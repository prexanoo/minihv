/* FCFS: fila unica em ordem de entrada em READY, sem preempcao.
 * E a politica de referencia do relatorio (RF-SCH-02). */
#include "sched/sched_policy.h"

#include <stdio.h>

#include "util/queue.h"

static void *fcfs_init(const SchedConfig *cfg)
{
    (void)cfg;
    return queue_create();
}

static void fcfs_enqueue(void *state, Vcb *vm, uint64_t tick)
{
    (void)tick;
    queue_push(state, vm);
}

static Vcb *fcfs_pick_next(void *state, int cpu, uint64_t tick)
{
    (void)cpu;
    (void)tick;
    return queue_pop(state);
}

static bool fcfs_on_tick(void *state, Vcb *running, uint64_t tick)
{
    (void)state;
    (void)running;
    (void)tick;
    return false;
}

static void fcfs_on_block(void *state, Vcb *vm, uint64_t tick)
{
    (void)state;
    (void)vm;
    (void)tick;
}

static void fcfs_on_exit(void *state, Vcb *vm, uint64_t tick)
{
    (void)tick;
    queue_remove(state, vm);
}

static void fcfs_remove(void *state, Vcb *vm)
{
    queue_remove(state, vm);
}

static void fcfs_on_clock(void *state, uint64_t tick)
{
    (void)state;
    (void)tick;
}

static void fcfs_describe(const void *state, const Vcb *vm, char *buf, size_t n)
{
    (void)state;
    (void)vm;
    snprintf(buf, n, "-");
}

static void fcfs_destroy(void *state)
{
    queue_destroy(state);
}

const SchedPolicy POLICY_FCFS = {
    .name = "FCFS",
    .uses_quantum = false,
    .init = fcfs_init,
    .enqueue = fcfs_enqueue,
    .pick_next = fcfs_pick_next,
    .on_tick = fcfs_on_tick,
    .on_block = fcfs_on_block,
    .destroy = fcfs_destroy,
    .on_exit = fcfs_on_exit,
    .remove = fcfs_remove,
    .on_clock = fcfs_on_clock,
    .describe = fcfs_describe,
};
