/* TA-08: o Gantt (out/ta08_gantt.csv) tem que bater tick a tick com o que
 * se reconstroi so a partir dos eventos DISPATCH / PREEMPT / STATE_CHANGE. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hv/hv.h"

#define MAX_TICKS 2000
#define MAX_CPUS 4
#define MAX_VMS 16
#define NAME_LEN 16

typedef struct Recon {
    char names[MAX_VMS][NAME_LEN];
    int cell[MAX_TICKS][MAX_CPUS]; /* id da VM ou -1 */
    int start[MAX_VMS];            /* tick do DISPATCH em aberto */
    int cpu_of[MAX_VMS];
} Recon;

static Recon rc;

static void fill(int vm, int ini, int fim)
{
    for (int t = ini; t <= fim; t++)
        rc.cell[t][rc.cpu_of[vm]] = vm;
}

static void on_event(void *ctx, const Event *ev)
{
    (void)ctx;
    int t = (int)ev->tick, id = ev->vm_id;
    if (ev->type == EV_DISPATCH) {
        snprintf(rc.names[id], NAME_LEN, "%s", ev->vm);
        rc.start[id] = t;
        rc.cpu_of[id] = ev->cpu;
    } else if (ev->type == EV_STATE_CHANGE && strncmp(ev->detail, "RUNNING->", 9) == 0) {
        /* preempcao acontece no passo 3 (a VM nao roda neste tick);
         * bloqueio e termino acontecem no passo 4 (a VM rodou neste tick) */
        bool preempt = strcmp(ev->detail, "RUNNING->READY") == 0;
        fill(id, rc.start[id], preempt ? t - 1 : t);
    }
}

static void check_csv(const char *path, int *linhas)
{
    FILE *fp = fopen(path, "r");
    assert(fp);
    char line[128];
    assert(fgets(line, sizeof line, fp) && strcmp(line, "tick,cpu,vm\n") == 0);
    int t, c;
    char nome[NAME_LEN];
    *linhas = 0;
    while (fscanf(fp, "%d,%d,%15s", &t, &c, nome) == 3) {
        int id = rc.cell[t][c];
        const char *esperado = id < 0 ? "-" : rc.names[id];
        if (strcmp(nome, esperado) != 0)
            printf("diferenca em t=%d cpu=%d: gantt=%s eventos=%s\n", t, c, nome, esperado);
        assert(strcmp(nome, esperado) == 0);
        (*linhas)++;
    }
    fclose(fp);
}

int main(void)
{
    memset(rc.cell, -1, sizeof rc.cell);
    FILE *out = tmpfile();
    HvOptions opt = {"conf/ta08.conf", NULL, out};
    Hv *hv = NULL;
    assert(hv_create(&opt, &hv) == HV_OK);
    hv_subscribe(hv, on_event, NULL);
    hv_run(hv, 0);
    uint64_t ticks = hv_clock(hv);
    assert(ticks < MAX_TICKS);
    hv_finish(hv, out);

    /* o comando do modo interativo tambem e do ext */
    assert(hv_ext_command(hv, "gantt", out));
    assert(!hv_ext_command(hv, "outro", out));
    hv_destroy(hv);

    int linhas;
    check_csv("out/ta08_gantt.csv", &linhas);
    assert(linhas == (int)ticks * MAX_CPUS);

    rewind(out);
    char line[256];
    int n_cpu0 = 0;
    while (fgets(line, sizeof line, out))
        if (strncmp(line, "cpu0", 4) == 0)
            n_cpu0++;
    /* impresso 2 vezes (fim + comando), uma linha cpu0 a cada 80 ticks */
    assert(n_cpu0 == 2 * (int)((ticks + 79) / 80));
    fclose(out);
    puts("test_ext: ok");
    return 0;
}
