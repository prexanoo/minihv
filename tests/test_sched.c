#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hv/hv.h"
#include "sched/sched.h"
#include "vm/vm.h"

/* ---------- teste direto da politica ---------- */

static Vcb *nova_vm(int id, const char *nome, int prio)
{
    char err[128];
    Program *p = NULL;
    assert(loader_parse_text("t.mv", "HALT\n", &p, err, sizeof err) == HV_OK);
    VmParams vp = {id, nome, prio, 16, 0};
    return vm_create(&vp, p, NULL, 0);
}

/* quantum 2, 1 pCPU: a gasta o saldo inicial (2) e vira OVER;
 * b ainda e UNDER e passa na frente mesmo estando atras na fila */
static void test_credit_under_antes_de_over(void)
{
    SchedConfig sc = {2, 1, 2026, NULL};
    Scheduler *s = sched_create("CREDIT", &sc);
    Vcb *a = nova_vm(0, "a", 0), *b = nova_vm(1, "b", 0);
    sched_enqueue(s, a, 0);
    sched_enqueue(s, b, 0);
    assert(sched_pick_next(s, 0, 0) == a);
    assert(!sched_on_tick(s, a, 0));
    assert(sched_on_tick(s, a, 1)); /* fim do quantum */
    sched_enqueue(s, a, 2);         /* fila: b a */
    assert(sched_pick_next(s, 0, 2) == b);
    sched_on_tick(s, b, 2);
    sched_on_tick(s, b, 3);
    sched_enqueue(s, b, 4); /* fila: a b, os dois com saldo 0 */
    assert(sched_pick_next(s, 0, 4) == a); /* nenhuma UNDER: primeira OVER */

    char desc[48];
    sched_describe(s, b, desc, sizeof desc);
    assert(strcmp(desc, "credit=0 OVER") == 0);

    /* recarga no tick 30: 30 creditos, pesos 10 e 10 -> +15 cada */
    sched_on_clock(s, 29);
    sched_on_clock(s, 30);
    sched_describe(s, b, desc, sizeof desc);
    assert(strcmp(desc, "credit=15 UNDER") == 0);
    sched_destroy(s);
    vm_destroy(a);
    vm_destroy(b);
}

static void test_factory(void)
{
    SchedConfig sc = {5, 4, 2026, NULL};
    Scheduler *s = sched_create("FCFS", &sc);
    assert(s && strcmp(sched_name(s), "FCFS") == 0 && !sched_uses_quantum(s));
    sched_destroy(s);
    s = sched_create("CREDIT", &sc);
    assert(s && sched_uses_quantum(s) && sched_quantum(s) == 5);
    sched_destroy(s);
    assert(sched_create("RR", &sc) == NULL);
}

/* ---------- sequencia de despachos x conta feita a mao ---------- */

#define MAX_DISP 64

typedef struct Trace {
    char seq[MAX_DISP * 8];
    int n;
} Trace;

static void on_event(void *ctx, const Event *ev)
{
    Trace *tr = ctx;
    if (ev->type != EV_DISPATCH)
        return;
    size_t len = strlen(tr->seq);
    snprintf(tr->seq + len, sizeof tr->seq - len, "%s%llu:%s", tr->n ? " " : "",
             (unsigned long long)ev->tick, ev->vm);
    tr->n++;
}

static void run_trace(const char *policy, Trace *tr)
{
    FILE *devnull = tmpfile();
    HvOptions opt = {"conf/ta02_1cpu.conf", policy, devnull};
    Hv *hv = NULL;
    memset(tr, 0, sizeof *tr);
    assert(hv_create(&opt, &hv) == HV_OK);
    hv_subscribe(hv, on_event, tr);
    hv_run(hv, 0);
    hv_destroy(hv);
    fclose(devnull);
}

/* 1 pCPU, q=5, VMs a b c d (23 ticks de CPU cada) chegando em 0 2 4 6 com
 * prioridades 0 3 6 9 (pesos 10 7 4 1). Recarga em t=30 e t=60:
 * +13 +9 +5 +1. Conta completa no README. */
static void test_credit_ta02(void)
{
    Trace tr;
    run_trace(NULL, &tr);
    const char *esperado = "0:a 5:b 10:c 15:d 20:a 25:b 30:c 35:d 40:a 45:b "
                           "50:a 55:c 60:b 65:a 68:b 71:d 76:c 81:d 86:c 89:d";
    if (strcmp(tr.seq, esperado) != 0)
        printf("obtido:   %s\nesperado: %s\n", tr.seq, esperado);
    assert(strcmp(tr.seq, esperado) == 0);
}

/* FCFS (referencia): cada VM roda os 23 ticks direto */
static void test_fcfs_ta02(void)
{
    Trace tr;
    run_trace("FCFS", &tr);
    assert(strcmp(tr.seq, "0:a 23:b 46:c 69:d") == 0);
}

int main(void)
{
    test_factory();
    test_credit_under_antes_de_over();
    test_credit_ta02();
    test_fcfs_ta02();
    puts("test_sched: ok");
    return 0;
}
