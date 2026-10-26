/* GANTT: guarda qual VM executou em cada pCPU a cada tick (evento EXEC)
 * e imprime o diagrama em texto, alem de gravar out/gantt.csv. */
#include "ext/ext.h"

#include <stdlib.h>
#include <string.h>

#define GANTT_WIDTH 80
#define GANTT_RULER_STEP 10
#define GANTT_IDLE '.'
#define NO_VM (-1)
#define NAME_LEN 16
#define PATH_LEN 256
#define MIN_TICKS 256
#define LABEL_W 7

struct Ext {
    bool enabled;
    int pcpus;
    char csv[PATH_LEN];
    int *cells; /* cells[tick * pcpus + cpu] = id da VM ou NO_VM */
    size_t cap_ticks;
    size_t n_ticks;
    char (*names)[NAME_LEN];
    int n_names;
};

Ext *ext_create(const ExtConfig *cfg)
{
    Ext *ext = calloc(1, sizeof *ext);
    if (!ext)
        return NULL;
    ext->enabled = cfg->enabled;
    ext->pcpus = cfg->pcpus;
    snprintf(ext->csv, sizeof ext->csv, "%s", cfg->csv_file ? cfg->csv_file : "out/gantt.csv");
    return ext;
}

void ext_destroy(Ext *ext)
{
    if (!ext)
        return;
    free(ext->cells);
    free(ext->names);
    free(ext);
}

const char *ext_name(void) { return "GANTT"; }
bool ext_enabled(const Ext *ext) { return ext->enabled; }

const char *ext_help(void)
{
    return "  gantt             mostra o diagrama de Gantt ate o tick atual (recurso extra)\n";
}

static bool ensure_ticks(Ext *ext, size_t tick)
{
    if (tick < ext->cap_ticks)
        return true;
    size_t cap = ext->cap_ticks ? ext->cap_ticks * 2 : MIN_TICKS;
    while (cap <= tick)
        cap *= 2;
    int *novo = realloc(ext->cells, cap * (size_t)ext->pcpus * sizeof *novo);
    if (!novo)
        return false;
    for (size_t i = ext->cap_ticks * (size_t)ext->pcpus; i < cap * (size_t)ext->pcpus; i++)
        novo[i] = NO_VM;
    ext->cells = novo;
    ext->cap_ticks = cap;
    return true;
}

static void save_name(Ext *ext, int id, const char *name)
{
    if (id < 0)
        return;
    if (id >= ext->n_names) {
        int n = id + 8;
        char(*novo)[NAME_LEN] = realloc(ext->names, (size_t)n * sizeof *novo);
        if (!novo)
            return;
        memset(novo + ext->n_names, 0, (size_t)(n - ext->n_names) * sizeof *novo);
        ext->names = novo;
        ext->n_names = n;
    }
    snprintf(ext->names[id], NAME_LEN, "%s", name);
}

void ext_on_event(void *ctx, const Event *ev)
{
    Ext *ext = ctx;
    if (!ext->enabled)
        return;
    switch (ev->type) {
    case EV_EXEC:
        /* o ext e criado depois das VMs (RF-HV-01), entao o nome vem daqui */
        if (ev->vm_id >= ext->n_names || !ext->names[ev->vm_id][0])
            save_name(ext, ev->vm_id, ev->vm);
        if (ev->cpu >= 0 && ev->cpu < ext->pcpus && ensure_ticks(ext, (size_t)ev->tick))
            ext->cells[ev->tick * (uint64_t)ext->pcpus + (uint64_t)ev->cpu] = ev->vm_id;
        break;
    case EV_TICK_END:
        if (ensure_ticks(ext, (size_t)ev->tick))
            ext->n_ticks = (size_t)ev->tick + 1;
        break;
    default:
        break;
    }
}

/* 0-9, depois A-Z; ids maiores viram '#' */
static char vm_char(int id)
{
    if (id == NO_VM)
        return GANTT_IDLE;
    if (id < 10)
        return (char)('0' + id);
    if (id < 36)
        return (char)('A' + id - 10);
    return '#';
}

static void print_ruler(FILE *out, size_t ini, size_t fim)
{
    char linha[GANTT_WIDTH + 16];
    size_t w = fim - ini;
    memset(linha, ' ', sizeof linha);
    for (size_t t = ini; t < fim; t += GANTT_RULER_STEP) {
        char num[16];
        int len = snprintf(num, sizeof num, "%llu", (unsigned long long)t);
        size_t pos = t - ini;
        for (int k = 0; k < len && pos + (size_t)k < w + 8; k++)
            linha[pos + (size_t)k] = num[k];
    }
    linha[w + 8] = '\0';
    fprintf(out, "%-*s%s\n", LABEL_W, "tick", linha);
    fprintf(out, "%-*s", LABEL_W, "");
    for (size_t t = ini; t < fim; t++)
        fputc((t % GANTT_RULER_STEP) == 0 ? '|' : ' ', out);
    fputc('\n', out);
}

static void print_block(const Ext *ext, FILE *out, size_t ini, size_t fim)
{
    print_ruler(out, ini, fim);
    for (int c = 0; c < ext->pcpus; c++) {
        fprintf(out, "cpu%-*d", LABEL_W - 3, c);
        for (size_t t = ini; t < fim; t++)
            fputc(vm_char(ext->cells[t * (size_t)ext->pcpus + (size_t)c]), out);
        fputc('\n', out);
    }
}

static void print_legend(const Ext *ext, FILE *out)
{
    fputs("legenda:", out);
    for (int i = 0; i < ext->n_names; i++)
        if (ext->names[i][0])
            fprintf(out, " %c=%s", vm_char(i), ext->names[i]);
    fprintf(out, " %c=ociosa\n", GANTT_IDLE);
}

static void print_gantt(const Ext *ext, FILE *out)
{
    fprintf(out, "== Gantt | %d pCPUs | %llu ticks ==\n", ext->pcpus,
            (unsigned long long)ext->n_ticks);
    for (size_t ini = 0; ini < ext->n_ticks; ini += GANTT_WIDTH) {
        size_t fim = ini + GANTT_WIDTH < ext->n_ticks ? ini + GANTT_WIDTH : ext->n_ticks;
        print_block(ext, out, ini, fim);
    }
    print_legend(ext, out);
}

static void write_csv(const Ext *ext)
{
    FILE *fp = fopen(ext->csv, "w");
    if (!fp) {
        fprintf(stderr, "aviso: nao consegui gravar %s\n", ext->csv);
        return;
    }
    fputs("tick,cpu,vm\n", fp);
    for (size_t t = 0; t < ext->n_ticks; t++) {
        for (int c = 0; c < ext->pcpus; c++) {
            int id = ext->cells[t * (size_t)ext->pcpus + (size_t)c];
            const char *nome = (id >= 0 && id < ext->n_names) ? ext->names[id] : "-";
            fprintf(fp, "%llu,%d,%s\n", (unsigned long long)t, c, nome);
        }
    }
    fclose(fp);
}

bool ext_command(Ext *ext, const char *cmd, FILE *out)
{
    if (strcmp(cmd, "gantt") != 0)
        return false;
    if (!ext->enabled)
        fputs("recurso GANTT desligado ([ext] enabled = false)\n", out);
    else
        print_gantt(ext, out);
    return true;
}

void ext_finish(Ext *ext, FILE *out)
{
    if (!ext->enabled)
        return;
    print_gantt(ext, out);
    write_csv(ext);
}
