#ifndef LOG_STATS_H
#define LOG_STATS_H

#include <stdint.h>
#include <stdio.h>

/* Monta o relatorio final (secao 6.5). Os modulos preenchem os numeros
 * e aqui so se calcula media e se formata. */

#define STATS_NA (-1LL) /* valor que nao se aplica, impresso como "-" */

typedef struct StatsVmRow {
    const char *name;
    const char *estado;
    long long chegada;
    long long fim;      /* STATS_NA se ainda nao terminou */
    long long espera;
    long long resposta; /* STATS_NA se nunca foi despachada */
    long long cpu;
    long long faults;   /* STATS_NA no modelo CONT */
    long long io;
} StatsVmRow;

typedef struct StatsReport {
    int escopo;
    const char *policy;
    int quantum;        /* 0 quando a politica nao usa quantum */
    const char *mem_desc;
    const char *disk;
    uint64_t ticks;
    int pcpus;
    uint64_t ctx_switches;
    const StatsVmRow *rows;
    int n_rows;
    const uint64_t *cpu_busy;
    const char *mem_line;  /* linha "Memoria: ..." pronta */
    const char *io_lines;  /* linhas de E/S prontas */
} StatsReport;

void stats_print_report(FILE *out, const StatsReport *r);

/* Formata com virgula decimal, como no exemplo do enunciado (40,0%). */
void stats_fmt(char *buf, size_t n, double v, int casas);

#endif
