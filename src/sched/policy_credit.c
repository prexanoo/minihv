/* CREDIT (inspirado no escalonador do Xen), politica do escopo 09.
 *
 * - peso = 10 - prioridade; VM admitida comeca com saldo = quantum
 * - a cada 30 ticks distribui 30 * pcpus creditos entre as VMs nao
 *   terminadas, proporcional ao peso (divisao inteira), saldo max 60
 * - cada tick em RUNNING custa 1 credito
 * - saldo > 0 = UNDER, senao OVER; pega a primeira UNDER da fila
 *   circular, e se nao tiver nenhuma, a primeira OVER
 * - preempcao so no fim do quantum (a VM volta para o fim da fila)
 */
#include "sched/sched_policy.h"

#include <stdio.h>
#include <string.h>

#include "util/queue.h"

#define CREDIT_PERIOD 30
#define CREDIT_MAX (2 * CREDIT_PERIOD)
#define WEIGHT_BASE 10

typedef struct Account {
    bool active; /* admitida e ainda nao terminou */
    int credit;
    int weight;
    int slice;   /* ticks usados do quantum atual */
    const Vcb *vm;
} Account;

typedef struct CreditState {
    Queue *ready;
    Account *acc; /* indexado pelo id da VM */
    int n_acc;
    int quantum;
    int pcpus;
    const EventSink *sink;
} CreditState;

static void *credit_init(const SchedConfig *cfg)
{
    CreditState *st = calloc(1, sizeof *st);
    if (!st)
        return NULL;
    st->ready = queue_create();
    if (!st->ready) {
        free(st);
        return NULL;
    }
    st->quantum = cfg->quantum;
    st->pcpus = cfg->pcpus;
    st->sink = cfg->sink;
    return st;
}

static void credit_destroy(void *state)
{
    CreditState *st = state;
    queue_destroy(st->ready);
    free(st->acc);
    free(st);
}

static Account *account(CreditState *st, const Vcb *vm)
{
    int id = vm_id(vm);
    if (id >= st->n_acc) {
        int n = id + 8;
        Account *novo = realloc(st->acc, (size_t)n * sizeof *novo);
        if (!novo)
            return NULL;
        memset(novo + st->n_acc, 0, (size_t)(n - st->n_acc) * sizeof *novo);
        st->acc = novo;
        st->n_acc = n;
    }
    return &st->acc[id];
}

static const Account *account_const(const CreditState *st, const Vcb *vm)
{
    int id = vm_id(vm);
    return id < st->n_acc ? &st->acc[id] : NULL;
}

static void credit_enqueue(void *state, Vcb *vm, uint64_t tick)
{
    (void)tick;
    CreditState *st = state;
    Account *a = account(st, vm);
    if (a && !a->active) { /* primeira vez que fica pronta: recem-admitida */
        a->active = true;
        a->credit = st->quantum;
        a->weight = WEIGHT_BASE - vm_priority(vm);
        a->vm = vm;
    }
    queue_push(st->ready, vm);
}

static Vcb *credit_pick_next(void *state, int cpu, uint64_t tick)
{
    (void)cpu;
    (void)tick;
    CreditState *st = state;
    if (queue_empty(st->ready))
        return NULL;
    size_t pos = 0; /* sem nenhuma UNDER fica a primeira (OVER) */
    for (size_t i = 0; i < queue_size(st->ready); i++) {
        const Account *a = account_const(st, queue_at(st->ready, i));
        if (a && a->credit > 0) {
            pos = i;
            break;
        }
    }
    Vcb *vm = queue_remove_at(st->ready, pos);
    Account *a = account(st, vm);
    if (a)
        a->slice = 0;
    return vm;
}

static bool credit_on_tick(void *state, Vcb *running, uint64_t tick)
{
    (void)tick;
    CreditState *st = state;
    Account *a = account(st, running);
    if (!a)
        return false;
    a->credit--;
    a->slice++;
    return a->slice >= st->quantum;
}

static void credit_on_block(void *state, Vcb *vm, uint64_t tick)
{
    (void)tick;
    Account *a = account(state, vm);
    if (a)
        a->slice = 0;
}

static void credit_on_exit(void *state, Vcb *vm, uint64_t tick)
{
    (void)tick;
    CreditState *st = state;
    Account *a = account(st, vm);
    if (a)
        a->active = false;
    queue_remove(st->ready, vm);
}

static void credit_remove(void *state, Vcb *vm)
{
    CreditState *st = state;
    queue_remove(st->ready, vm);
}

static void emit_credit(const CreditState *st, const Account *a, int add, uint64_t tick)
{
    Event ev = event_make(EV_CREDIT, "sched", tick);
    ev.vm_id = vm_id(a->vm);
    ev.vm = vm_name(a->vm);
    snprintf(ev.detail, sizeof ev.detail, "add=%d credit=%d", add, a->credit);
    event_emit(st->sink, &ev);
}

/* recarga periodica dos creditos, proporcional ao peso */
static void credit_on_clock(void *state, uint64_t tick)
{
    CreditState *st = state;
    if (tick == 0 || tick % CREDIT_PERIOD != 0)
        return;
    int soma = 0;
    for (int i = 0; i < st->n_acc; i++)
        if (st->acc[i].active)
            soma += st->acc[i].weight;
    if (soma == 0)
        return;
    int total = CREDIT_PERIOD * st->pcpus;
    for (int i = 0; i < st->n_acc; i++) {
        Account *a = &st->acc[i];
        if (!a->active)
            continue;
        int add = total * a->weight / soma;
        a->credit += add;
        if (a->credit > CREDIT_MAX)
            a->credit = CREDIT_MAX;
        emit_credit(st, a, add, tick);
    }
}

static void credit_describe(const void *state, const Vcb *vm, char *buf, size_t n)
{
    const Account *a = account_const(state, vm);
    if (!a || !a->active) {
        snprintf(buf, n, "-");
        return;
    }
    snprintf(buf, n, "credit=%d %s", a->credit, a->credit > 0 ? "UNDER" : "OVER");
}

const SchedPolicy POLICY_CREDIT = {
    .name = "CREDIT",
    .uses_quantum = true,
    .init = credit_init,
    .enqueue = credit_enqueue,
    .pick_next = credit_pick_next,
    .on_tick = credit_on_tick,
    .on_block = credit_on_block,
    .destroy = credit_destroy,
    .on_exit = credit_on_exit,
    .remove = credit_remove,
    .on_clock = credit_on_clock,
    .describe = credit_describe,
};
