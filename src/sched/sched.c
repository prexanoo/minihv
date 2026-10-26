#include "sched/sched.h"

#include <string.h>

#include "sched/sched_policy.h"

struct Scheduler {
    const SchedPolicy *pol;
    void *state;
    int quantum;
};

static const SchedPolicy *const POLICIES[] = {&POLICY_FCFS, &POLICY_CREDIT};

const SchedPolicy *sched_policy_create(const char *name)
{
    for (size_t i = 0; i < sizeof POLICIES / sizeof POLICIES[0]; i++)
        if (strcmp(POLICIES[i]->name, name) == 0)
            return POLICIES[i];
    return NULL;
}

Scheduler *sched_create(const char *policy, const SchedConfig *cfg)
{
    const SchedPolicy *pol = sched_policy_create(policy);
    if (!pol)
        return NULL;
    Scheduler *s = malloc(sizeof *s);
    if (!s)
        return NULL;
    s->pol = pol;
    s->quantum = cfg->quantum;
    s->state = pol->init(cfg);
    if (!s->state) {
        free(s);
        return NULL;
    }
    return s;
}

void sched_destroy(Scheduler *s)
{
    if (!s)
        return;
    s->pol->destroy(s->state);
    free(s);
}

const char *sched_name(const Scheduler *s) { return s->pol->name; }
bool sched_uses_quantum(const Scheduler *s) { return s->pol->uses_quantum; }
int sched_quantum(const Scheduler *s) { return s->quantum; }

void sched_enqueue(Scheduler *s, Vcb *vm, uint64_t tick)
{
    s->pol->enqueue(s->state, vm, tick);
}

Vcb *sched_pick_next(Scheduler *s, int cpu, uint64_t tick)
{
    return s->pol->pick_next(s->state, cpu, tick);
}

bool sched_on_tick(Scheduler *s, Vcb *running, uint64_t tick)
{
    return s->pol->on_tick(s->state, running, tick);
}

void sched_on_block(Scheduler *s, Vcb *vm, uint64_t tick)
{
    s->pol->on_block(s->state, vm, tick);
}

void sched_on_exit(Scheduler *s, Vcb *vm, uint64_t tick)
{
    s->pol->on_exit(s->state, vm, tick);
}

void sched_remove(Scheduler *s, Vcb *vm)
{
    s->pol->remove(s->state, vm);
}

void sched_on_clock(Scheduler *s, uint64_t tick)
{
    s->pol->on_clock(s->state, tick);
}

void sched_describe(const Scheduler *s, const Vcb *vm, char *buf, size_t n)
{
    s->pol->describe(s->state, vm, buf, n);
}
