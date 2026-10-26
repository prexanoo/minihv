#ifndef SCHED_SCHED_POLICY_H
#define SCHED_SCHED_POLICY_H

#include "sched/sched.h"

/* Padrao Strategy: cada politica preenche esta tabela de funcoes.
 * As 6 primeiras sao as minimas do RF-SCH-01; as outras foram
 * necessarias para o CREDIT (saida da VM, pause e a recarga periodica). */
typedef struct SchedPolicy {
    const char *name;
    bool uses_quantum;
    void *(*init)(const SchedConfig *cfg);
    void (*enqueue)(void *state, Vcb *vm, uint64_t tick);
    Vcb *(*pick_next)(void *state, int cpu, uint64_t tick);
    bool (*on_tick)(void *state, Vcb *running, uint64_t tick); /* true = preemptar */
    void (*on_block)(void *state, Vcb *vm, uint64_t tick);
    void (*destroy)(void *state);
    void (*on_exit)(void *state, Vcb *vm, uint64_t tick);
    void (*remove)(void *state, Vcb *vm);
    void (*on_clock)(void *state, uint64_t tick);
    void (*describe)(const void *state, const Vcb *vm, char *buf, size_t n);
} SchedPolicy;

const SchedPolicy *sched_policy_create(const char *name); /* Factory */

extern const SchedPolicy POLICY_FCFS;
extern const SchedPolicy POLICY_CREDIT;

#endif
