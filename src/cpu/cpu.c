#include "cpu/cpu.h"

#include <stdlib.h>
#include <string.h>

typedef struct Pcpu {
    int32_t regs[MV_NREGS];
    int32_t pc;
    Vcb *vm;        /* VM rodando agora (NULL = livre) */
    const Vcb *last; /* ultimo contexto carregado, para contar trocas */
    uint64_t busy;
} Pcpu;

struct Cpu {
    Pcpu *p;
    int n;
    uint64_t ctx_switches;
    const EventSink *sink;
};

Cpu *cpu_create(int n, const EventSink *sink)
{
    Cpu *cpu = malloc(sizeof *cpu);
    if (!cpu)
        return NULL;
    cpu->p = calloc((size_t)n, sizeof *cpu->p);
    if (!cpu->p) {
        free(cpu);
        return NULL;
    }
    cpu->n = n;
    cpu->ctx_switches = 0;
    cpu->sink = sink;
    return cpu;
}

void cpu_destroy(Cpu *cpu)
{
    if (!cpu)
        return;
    free(cpu->p);
    free(cpu);
}

Vcb *cpu_current(const Cpu *cpu, int i) { return cpu->p[i].vm; }
int32_t cpu_pc(const Cpu *cpu, int i) { return cpu->p[i].pc; }
uint64_t cpu_busy_ticks(const Cpu *cpu, int i) { return cpu->p[i].busy; }
uint64_t cpu_ctx_switches(const Cpu *cpu) { return cpu->ctx_switches; }

/* Conta troca de contexto quando entra na pCPU uma VM diferente da
 * ultima que estava carregada nela (re-despacho da mesma VM nao conta). */
void cpu_switch_in(Cpu *cpu, int i, Vcb *vm, uint64_t tick)
{
    Pcpu *c = &cpu->p[i];
    vm_get_context(vm, c->regs, &c->pc);
    c->vm = vm;
    if (c->last == vm)
        return;
    cpu->ctx_switches++;
    Event ev = event_make(EV_CTX_SWITCH, "cpu", tick);
    ev.vm_id = vm_id(vm);
    ev.vm = vm_name(vm);
    ev.cpu = i;
    snprintf(ev.detail, sizeof ev.detail, "cpu=%d out=%s in=%s", i,
             c->last ? vm_name(c->last) : "-", vm_name(vm));
    event_emit(cpu->sink, &ev);
    c->last = vm;
}

Vcb *cpu_switch_out(Cpu *cpu, int i)
{
    Pcpu *c = &cpu->p[i];
    Vcb *vm = c->vm;
    if (vm)
        vm_set_context(vm, c->regs, c->pc);
    c->vm = NULL;
    return vm;
}

/* soma com estouro definido (complemento de dois), sem UB de int com sinal */
static int32_t wrap_add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t wrap_sub(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a - (uint32_t)b);
}

static CpuResult jump(Pcpu *c, int32_t target, int len)
{
    if (target < 0 || target >= len)
        return CPU_BAD_INSTR; /* RF-CPU-04: salto para fora do programa */
    c->pc = target;
    return CPU_OK;
}

static CpuResult do_io(Pcpu *c, Io *io, DevId dev, int32_t arg, uint64_t tick)
{
    if (io_request(io, c->vm, dev, arg, tick) != HV_OK)
        return CPU_BAD_INSTR;
    vm_count_io(c->vm);
    return CPU_BLOCKED;
}

static CpuResult mem_result(HvStatus st)
{
    return st == HV_OK ? CPU_OK : CPU_PROT_FAULT;
}

static CpuResult execute(Pcpu *c, const Instr *in, int len, Mem *mem, Io *io, uint64_t tick)
{
    int32_t *r = c->regs;
    switch (in->op) {
    case OP_MOV: r[in->a] = in->b; return CPU_OK;
    case OP_ADD: r[in->a] = wrap_add(r[in->a], r[in->b]); return CPU_OK;
    case OP_ADDI: r[in->a] = wrap_add(r[in->a], in->b); return CPU_OK;
    case OP_SUB: r[in->a] = wrap_sub(r[in->a], r[in->b]); return CPU_OK;
    case OP_LOAD: return mem_result(mem_read(mem, c->vm, in->b, &r[in->a]));
    case OP_LOADR: return mem_result(mem_read(mem, c->vm, r[in->b], &r[in->a]));
    case OP_STORE: return mem_result(mem_write(mem, c->vm, in->b, r[in->a]));
    case OP_STORER: return mem_result(mem_write(mem, c->vm, r[in->b], r[in->a]));
    case OP_JMP: return jump(c, in->a, len);
    case OP_JZ: return r[in->a] == 0 ? jump(c, in->b, len) : CPU_OK;
    case OP_JNZ: return r[in->a] != 0 ? jump(c, in->b, len) : CPU_OK;
    case OP_OUT: return do_io(c, io, DEV_CONSOLE, r[in->a], tick);
    case OP_DISK: return do_io(c, io, DEV_DISK, r[in->a], tick);
    case OP_NET: return do_io(c, io, DEV_NET, in->a, tick);
    case OP_HALT: return CPU_HALT;
    }
    return CPU_BAD_INSTR;
}

/* RF-CPU-02: a busca passa pelo mem (endereco virtual = PC). A palavra lida
 * so serve para checar a protecao; a instrucao ja vem decodificada do loader. */
CpuResult cpu_step(Cpu *cpu, int i, Mem *mem, Io *io, uint64_t tick)
{
    Pcpu *c = &cpu->p[i];
    const Program *prog = vm_program(c->vm);
    int len = program_len(prog);
    int32_t word;
    c->busy++;
    if (c->pc < 0 || c->pc >= len)
        return CPU_BAD_INSTR;
    if (mem_read(mem, c->vm, c->pc, &word) != HV_OK)
        return CPU_PROT_FAULT;
    const Instr *in = program_at(prog, c->pc);
    c->pc++;
    return execute(c, in, len, mem, io, tick);
}
