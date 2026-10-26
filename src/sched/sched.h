#ifndef SCHED_SCHED_H
#define SCHED_SCHED_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "hv/events.h"
#include "vm/vm.h"

/* O escalonador so conhece VCBs: nao inclui mem nem io. */

typedef struct SchedConfig {
    int quantum;
    int pcpus;
    uint32_t seed;
    const EventSink *sink;
} SchedConfig;

typedef struct Scheduler Scheduler;

/* RF-SCH-02: escolhe a politica pelo nome (Factory). NULL se nao existir. */
Scheduler *sched_create(const char *policy, const SchedConfig *cfg);
void sched_destroy(Scheduler *s);

const char *sched_name(const Scheduler *s);
bool sched_uses_quantum(const Scheduler *s);
int sched_quantum(const Scheduler *s);

void sched_enqueue(Scheduler *s, Vcb *vm, uint64_t tick);
Vcb *sched_pick_next(Scheduler *s, int cpu, uint64_t tick);
/* chamado depois de cada tick em RUNNING; true = preemptar */
bool sched_on_tick(Scheduler *s, Vcb *running, uint64_t tick);
void sched_on_block(Scheduler *s, Vcb *vm, uint64_t tick);
void sched_on_exit(Scheduler *s, Vcb *vm, uint64_t tick);
void sched_remove(Scheduler *s, Vcb *vm);
void sched_on_clock(Scheduler *s, uint64_t tick);
void sched_describe(const Scheduler *s, const Vcb *vm, char *buf, size_t n);

#endif
