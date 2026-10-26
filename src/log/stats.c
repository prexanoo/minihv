#include "log/stats.h"

#include <string.h>

#define NAME_MIN_W 4
#define ESTADO_W 20

void stats_fmt(char *buf, size_t n, double v, int casas)
{
    snprintf(buf, n, "%.*f", casas, v);
    for (char *p = buf; *p; p++)
        if (*p == '.')
            *p = ',';
}

static void print_num(FILE *out, long long v, int w)
{
    if (v == STATS_NA)
        fprintf(out, "%-*s", w, "-");
    else
        fprintf(out, "%-*lld", w, v);
}

static int name_width(const StatsReport *r)
{
    int w = NAME_MIN_W;
    for (int i = 0; i < r->n_rows; i++) {
        int len = (int)strlen(r->rows[i].name);
        if (len > w)
            w = len;
    }
    return w + 2;
}

static void print_header(FILE *out, const StatsReport *r)
{
    fprintf(out, "== MiniHV | escopo %02d | policy=%s", r->escopo, r->policy);
    if (r->quantum > 0)
        fprintf(out, " q=%d", r->quantum);
    fprintf(out, " | mem=%s | disk=%s ==\n", r->mem_desc, r->disk);
    fprintf(out, "ticks=%llu  pcpus=%d  ctx_switches=%llu\n",
            (unsigned long long)r->ticks, r->pcpus, (unsigned long long)r->ctx_switches);
}

static void print_rows(FILE *out, const StatsReport *r)
{
    int nw = name_width(r);
    fprintf(out, "%-*s%-*s%-9s%-6s%-12s%-8s%-10s%-6s%-8s%s\n", nw, "VM", ESTADO_W,
            "estado", "chegada", "fim", "turnaround", "espera", "resposta", "cpu",
            "faults", "io");
    for (int i = 0; i < r->n_rows; i++) {
        const StatsVmRow *v = &r->rows[i];
        long long ta = v->fim == STATS_NA ? STATS_NA : v->fim - v->chegada;
        fprintf(out, "%-*s%-*s", nw, v->name, ESTADO_W, v->estado);
        print_num(out, v->chegada, 9);
        print_num(out, v->fim, 6);
        print_num(out, ta, 12);
        print_num(out, v->espera, 8);
        print_num(out, v->resposta, 10);
        print_num(out, v->cpu, 6);
        print_num(out, v->faults, 8);
        fprintf(out, "%lld\n", v->io);
    }
}

static void print_cpu(FILE *out, const StatsReport *r)
{
    char buf[32];
    fputs("CPU   utilização:", out);
    for (int i = 0; i < r->pcpus; i++) {
        double u = r->ticks ? 100.0 * (double)r->cpu_busy[i] / (double)r->ticks : 0.0;
        stats_fmt(buf, sizeof buf, u, 1);
        fprintf(out, "  cpu%d=%s%%", i, buf);
    }
    fputc('\n', out);
}

/* medias consideram so as VMs que ja terminaram (turnaround e espera)
 * e as que ja foram despachadas (resposta) */
static void print_means(FILE *out, const StatsReport *r)
{
    double ta = 0, esp = 0, resp = 0;
    int n_fim = 0, n_resp = 0;
    for (int i = 0; i < r->n_rows; i++) {
        const StatsVmRow *v = &r->rows[i];
        if (v->fim != STATS_NA) {
            ta += (double)(v->fim - v->chegada);
            esp += (double)v->espera;
            n_fim++;
        }
        if (v->resposta != STATS_NA) {
            resp += (double)v->resposta;
            n_resp++;
        }
    }
    char b1[32], b2[32], b3[32], b4[32];
    stats_fmt(b1, sizeof b1, n_fim ? ta / n_fim : 0.0, 1);
    stats_fmt(b2, sizeof b2, n_fim ? esp / n_fim : 0.0, 1);
    stats_fmt(b3, sizeof b3, n_resp ? resp / n_resp : 0.0, 1);
    stats_fmt(b4, sizeof b4, r->ticks ? (double)n_fim / (double)r->ticks : 0.0, 4);
    fprintf(out, "Médias: turnaround=%s  espera=%s  resposta=%s  throughput=%s VM/tick\n",
            b1, b2, b3, b4);
}

void stats_print_report(FILE *out, const StatsReport *r)
{
    print_header(out, r);
    print_rows(out, r);
    print_cpu(out, r);
    print_means(out, r);
    fputs(r->mem_line, out);
    fputs(r->io_lines, out);
}
