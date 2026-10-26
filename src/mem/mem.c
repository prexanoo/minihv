#include "mem/mem.h"

#include <stdlib.h>
#include <string.h>

#include "log/stats.h"
#include "mem/mem_policy.h"
#include "util/list.h"

typedef struct Part {
    int base;
    int size;
    Vcb *vm;
} Part;

struct Mem {
    int32_t *phys;
    int phys_words;
    List *parts; /* particoes ocupadas, em ordem de endereco */
    const AllocPolicy *pol;
    const EventSink *sink;
    int compactions;
    uint64_t samples;
    double occ_sum;
    uint64_t frag_samples;
    double frag_sum;
};

/* RF-MEM-01: vetor fisico alocado uma unica vez */
Mem *mem_create(const MemConfig *cfg)
{
    const AllocPolicy *pol = mem_policy_create(cfg->allocation);
    if (!pol)
        return NULL;
    Mem *mem = calloc(1, sizeof *mem);
    if (!mem)
        return NULL;
    mem->phys = calloc((size_t)cfg->phys_words, sizeof *mem->phys);
    mem->parts = list_create();
    if (!mem->phys || !mem->parts) {
        mem_destroy(mem);
        return NULL;
    }
    mem->phys_words = cfg->phys_words;
    mem->pol = pol;
    mem->sink = cfg->sink;
    return mem;
}

void mem_destroy(Mem *mem)
{
    if (!mem)
        return;
    if (mem->parts) {
        for (size_t i = 0; i < list_size(mem->parts); i++)
            free(list_get(mem->parts, i));
        list_destroy(mem->parts);
    }
    free(mem->phys);
    free(mem);
}

static Part *part_at(const Mem *mem, size_t i)
{
    return list_get(mem->parts, i);
}

static void emit(const Mem *mem, EventType t, const Vcb *vm, uint64_t tick, const char *det)
{
    Event ev = event_make(t, "mem", tick);
    if (vm) {
        ev.vm_id = vm_id(vm);
        ev.vm = vm_name(vm);
    }
    snprintf(ev.detail, sizeof ev.detail, "%s", det);
    event_emit(mem->sink, &ev);
}

/* os buracos sao os espacos entre particoes; assim a coalescencia de
 * buracos vizinhos acontece sozinha quando uma particao sai da lista */
static int build_holes(const Mem *mem, Hole *out)
{
    int n = 0, next = 0;
    for (size_t i = 0; i < list_size(mem->parts); i++) {
        const Part *p = part_at(mem, i);
        if (p->base > next)
            out[n++] = (Hole){next, p->base - next};
        next = p->base + p->size;
    }
    if (next < mem->phys_words)
        out[n++] = (Hole){next, mem->phys_words - next};
    return n;
}

int mem_free_words(const Mem *mem)
{
    int used = 0;
    for (size_t i = 0; i < list_size(mem->parts); i++)
        used += part_at(mem, i)->size;
    return mem->phys_words - used;
}

int mem_largest_hole(const Mem *mem)
{
    Hole *holes = malloc((list_size(mem->parts) + 1) * sizeof *holes);
    if (!holes)
        return 0;
    int n = build_holes(mem, holes), maior = 0;
    for (int i = 0; i < n; i++)
        if (holes[i].size > maior)
            maior = holes[i].size;
    free(holes);
    return maior;
}

/* RF-MEM-07: empurra as particoes para o inicio, em ordem de endereco */
static int compact(Mem *mem, uint64_t tick)
{
    int next = 0, moved = 0;
    for (size_t i = 0; i < list_size(mem->parts); i++) {
        Part *p = part_at(mem, i);
        if (p->base != next) {
            memmove(&mem->phys[next], &mem->phys[p->base], (size_t)p->size * sizeof *mem->phys);
            p->base = next;
            vm_set_partition(p->vm, p->base, p->size);
            moved++;
        }
        next += p->size;
    }
    mem->compactions++;
    char det[EVENT_DETAIL_LEN];
    snprintf(det, sizeof det, "moved=%d ticks=%d free=%d", moved, moved, mem->phys_words - next);
    emit(mem, EV_COMPACT, NULL, tick, det);
    return moved;
}

/* zera a particao e grava o opcode de cada instrucao nas palavras 0..n-1
 * (RF-MEM-02: o programa ocupa o inicio do espaco da VM) */
static void load_image(Mem *mem, const Part *p)
{
    const Program *prog = vm_program(p->vm);
    memset(&mem->phys[p->base], 0, (size_t)p->size * sizeof *mem->phys);
    for (int i = 0; i < program_len(prog) && i < p->size; i++)
        mem->phys[p->base + i] = (int32_t)program_at(prog, i)->op;
}

static HvStatus place(Mem *mem, Vcb *vm, int base, uint64_t tick)
{
    Part *p = malloc(sizeof *p);
    if (!p)
        return HV_ERR_NOMEM;
    p->base = base;
    p->size = vm_mem_words(vm);
    p->vm = vm;
    size_t pos = 0;
    while (pos < list_size(mem->parts) && part_at(mem, pos)->base < base)
        pos++;
    if (!list_insert(mem->parts, pos, p)) {
        free(p);
        return HV_ERR_NOMEM;
    }
    vm_set_partition(vm, p->base, p->size);
    load_image(mem, p);
    char det[EVENT_DETAIL_LEN];
    snprintf(det, sizeof det, "base=%d size=%d", p->base, p->size);
    emit(mem, EV_MEM_ALLOC, vm, tick, det);
    return HV_OK;
}

HvStatus mem_alloc(Mem *mem, Vcb *vm, uint64_t tick, int *moved)
{
    int size = vm_mem_words(vm);
    *moved = 0;
    Hole *holes = malloc((list_size(mem->parts) + 1) * sizeof *holes);
    if (!holes)
        return HV_ERR_NOMEM;
    int n = build_holes(mem, holes);
    int idx = mem->pol->choose(holes, n, size);
    if (idx < 0 && mem_free_words(mem) >= size) {
        /* cabe no total mas nao em nenhum buraco: fragmentacao externa */
        *moved = compact(mem, tick);
        n = build_holes(mem, holes);
        idx = mem->pol->choose(holes, n, size);
    }
    HvStatus st = HV_ERR_NOSPACE;
    if (idx >= 0)
        st = place(mem, vm, holes[idx].base, tick);
    free(holes);
    return st;
}

void mem_release(Mem *mem, Vcb *vm, uint64_t tick)
{
    for (size_t i = 0; i < list_size(mem->parts); i++) {
        Part *p = part_at(mem, i);
        if (p->vm != vm)
            continue;
        char det[EVENT_DETAIL_LEN];
        snprintf(det, sizeof det, "base=%d size=%d", p->base, p->size);
        emit(mem, EV_MEM_FREE, vm, tick, det);
        list_remove_at(mem->parts, i);
        free(p);
        vm_set_partition(vm, 0, 0);
        return;
    }
}

/* RF-VM-05: endereco fora de [0, limite) e violacao de protecao */
HvStatus mem_translate(const Mem *mem, const Vcb *vm, int32_t vaddr, int *paddr)
{
    int limit = vm_limit(vm);
    if (vaddr < 0 || vaddr >= limit)
        return HV_ERR_PROTECTION;
    int fis = vm_base(vm) + vaddr;
    if (fis >= mem->phys_words)
        return HV_ERR_PROTECTION;
    *paddr = fis;
    return HV_OK;
}

HvStatus mem_read(const Mem *mem, const Vcb *vm, int32_t vaddr, int32_t *val)
{
    int pa;
    HvStatus st = mem_translate(mem, vm, vaddr, &pa);
    if (st == HV_OK)
        *val = mem->phys[pa];
    return st;
}

HvStatus mem_write(Mem *mem, const Vcb *vm, int32_t vaddr, int32_t val)
{
    int pa;
    HvStatus st = mem_translate(mem, vm, vaddr, &pa);
    if (st == HV_OK)
        mem->phys[pa] = val;
    return st;
}

void mem_sample(Mem *mem)
{
    int livre = mem_free_words(mem);
    mem->samples++;
    mem->occ_sum += (double)(mem->phys_words - livre) / (double)mem->phys_words;
    if (livre > 0) { /* fragmentacao = 1 - maior buraco / livre total */
        mem->frag_samples++;
        mem->frag_sum += 1.0 - (double)mem_largest_hole(mem) / (double)livre;
    }
}

int mem_compactions(const Mem *mem)
{
    return mem->compactions;
}

const char *mem_policy_name(const Mem *mem)
{
    return mem->pol->name;
}

void mem_print(const Mem *mem, FILE *out)
{
    Hole *holes = malloc((list_size(mem->parts) + 1) * sizeof *holes);
    if (!holes)
        return;
    int n = build_holes(mem, holes);
    fprintf(out, "memoria CONT/%s: %d palavras, livre=%d, maior buraco=%d, compactacoes=%d\n",
            mem->pol->name, mem->phys_words, mem_free_words(mem), mem_largest_hole(mem),
            mem->compactions);
    for (size_t i = 0; i < list_size(mem->parts); i++) {
        const Part *p = part_at(mem, i);
        fprintf(out, "  [%5d..%5d) %-10s %d palavras\n", p->base, p->base + p->size,
                vm_name(p->vm), p->size);
    }
    for (int i = 0; i < n; i++)
        fprintf(out, "  [%5d..%5d) (livre)    %d palavras\n", holes[i].base,
                holes[i].base + holes[i].size, holes[i].size);
    free(holes);
}

void mem_report_line(const Mem *mem, char *buf, size_t n)
{
    char frag[32], occ[32];
    double f = mem->frag_samples ? 100.0 * mem->frag_sum / (double)mem->frag_samples : 0.0;
    double o = mem->samples ? 100.0 * mem->occ_sum / (double)mem->samples : 0.0;
    stats_fmt(frag, sizeof frag, f, 1);
    stats_fmt(occ, sizeof occ, o, 1);
    snprintf(buf, n, "Memória: fragmentação_externa_média=%s%%  compactações=%d  ocupação_média=%s%%\n",
             frag, mem->compactions, occ);
}
