#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VM_NAME_LEN 16

struct Vcb {
    int id;
    char name[VM_NAME_LEN];
    VmState state;
    int priority;
    int32_t regs[MV_NREGS];
    int32_t pc;
    Program *prog;
    int mem_words;
    int base;
    int limit;
    uint64_t arrival;
    bool dispatched;
    uint64_t first_dispatch;
    uint64_t end;
    ExitReason reason;
    uint64_t wait_ticks;
    uint64_t cpu_ticks;
    uint64_t io_count;
    const EventSink *sink;
};

/* Padrao State: TRANSICAO_VALIDA[de][para] */
static const bool TRANSICAO_VALIDA[VM_N_STATES][VM_N_STATES] = {
    /*            NEW    READY  RUN    BLOCK  PAUSE  TERM */
    /* NEW    */ {false, true,  false, false, false, true},
    /* READY  */ {false, false, true,  false, true,  true},
    /* RUNNING*/ {false, true,  false, true,  true,  true},
    /* BLOCKED*/ {false, true,  false, false, false, true},
    /* PAUSED */ {false, true,  false, false, false, true},
    /* TERM   */ {false, false, false, false, false, false},
};

static void emit(const Vcb *vm, EventType t, uint64_t tick, LogLevel lv, const char *det)
{
    Event ev = event_make(t, "vm", tick);
    ev.level = lv;
    ev.vm_id = vm->id;
    ev.vm = vm->name;
    snprintf(ev.detail, sizeof ev.detail, "%s", det);
    event_emit(vm->sink, &ev);
}

Vcb *vm_create(const VmParams *p, Program *prog, const EventSink *sink, uint64_t tick)
{
    Vcb *vm = calloc(1, sizeof *vm);
    if (!vm)
        return NULL;
    vm->id = p->id;
    snprintf(vm->name, sizeof vm->name, "%s", p->name);
    vm->state = VM_NEW;
    vm->priority = p->priority;
    vm->prog = prog;
    vm->mem_words = p->mem_words;
    vm->arrival = p->arrival;
    vm->sink = sink;
    char det[EVENT_DETAIL_LEN];
    snprintf(det, sizeof det, "program=%s prio=%d mem=%d arrival=%llu", program_path(prog),
             p->priority, p->mem_words, (unsigned long long)p->arrival);
    emit(vm, EV_VM_CREATE, tick, LOG_INFO, det);
    return vm;
}

void vm_destroy(Vcb *vm)
{
    if (!vm)
        return;
    program_destroy(vm->prog);
    free(vm);
}

int vm_id(const Vcb *vm) { return vm->id; }
const char *vm_name(const Vcb *vm) { return vm->name; }
VmState vm_state(const Vcb *vm) { return vm->state; }
int vm_priority(const Vcb *vm) { return vm->priority; }
int vm_mem_words(const Vcb *vm) { return vm->mem_words; }
uint64_t vm_arrival(const Vcb *vm) { return vm->arrival; }
const Program *vm_program(const Vcb *vm) { return vm->prog; }
ExitReason vm_exit_reason(const Vcb *vm) { return vm->reason; }

bool vm_is_admitted(const Vcb *vm)
{
    return vm->state != VM_NEW && vm->state != VM_TERMINATED;
}

void vm_get_context(const Vcb *vm, int32_t regs[MV_NREGS], int32_t *pc)
{
    memcpy(regs, vm->regs, sizeof vm->regs);
    *pc = vm->pc;
}

void vm_set_context(Vcb *vm, const int32_t regs[MV_NREGS], int32_t pc)
{
    memcpy(vm->regs, regs, sizeof vm->regs);
    vm->pc = pc;
}

void vm_set_partition(Vcb *vm, int base, int limit)
{
    vm->base = base;
    vm->limit = limit;
}

int vm_base(const Vcb *vm) { return vm->base; }
int vm_limit(const Vcb *vm) { return vm->limit; }

HvStatus vm_set_state(Vcb *vm, VmState to, uint64_t tick)
{
    if (!TRANSICAO_VALIDA[vm->state][to])
        return HV_ERR_INVALID;
    char det[EVENT_DETAIL_LEN];
    snprintf(det, sizeof det, "%s->%s", vm_state_name(vm->state), vm_state_name(to));
    vm->state = to;
    emit(vm, EV_STATE_CHANGE, tick, LOG_INFO, det);
    return HV_OK;
}

HvStatus vm_admit(Vcb *vm, uint64_t tick)
{
    HvStatus st = vm_set_state(vm, VM_READY, tick);
    if (st == HV_OK) {
        char det[EVENT_DETAIL_LEN];
        snprintf(det, sizeof det, "base=%d limit=%d", vm->base, vm->limit);
        emit(vm, EV_VM_ADMIT, tick, LOG_INFO, det);
    }
    return st;
}

/* RF-VM-06: guarda o motivo e o tick de termino */
HvStatus vm_terminate(Vcb *vm, ExitReason why, uint64_t tick)
{
    HvStatus st = vm_set_state(vm, VM_TERMINATED, tick);
    if (st != HV_OK)
        return st;
    vm->reason = why;
    vm->end = tick;
    char det[EVENT_DETAIL_LEN];
    snprintf(det, sizeof det, "reason=%s pc=%d", vm_exit_name(why), (int)vm->pc);
    LogLevel lv = (why == EXIT_HALT) ? LOG_INFO : LOG_WARN;
    emit(vm, EV_VM_EXIT, tick, lv, det);
    return HV_OK;
}

void vm_on_dispatch(Vcb *vm, uint64_t tick)
{
    if (!vm->dispatched) {
        vm->dispatched = true;
        vm->first_dispatch = tick;
    }
}

void vm_count_ready_tick(Vcb *vm) { vm->wait_ticks++; }
void vm_count_cpu_tick(Vcb *vm) { vm->cpu_ticks++; }
void vm_count_io(Vcb *vm) { vm->io_count++; }
uint64_t vm_wait_ticks(const Vcb *vm) { return vm->wait_ticks; }
uint64_t vm_cpu_ticks(const Vcb *vm) { return vm->cpu_ticks; }
uint64_t vm_io_count(const Vcb *vm) { return vm->io_count; }
bool vm_was_dispatched(const Vcb *vm) { return vm->dispatched; }
uint64_t vm_first_dispatch(const Vcb *vm) { return vm->first_dispatch; }
uint64_t vm_end_tick(const Vcb *vm) { return vm->end; }

const char *vm_state_name(VmState s)
{
    static const char *const NAMES[] = {"NEW",     "READY",  "RUNNING",
                                        "BLOCKED", "PAUSED", "TERMINATED"};
    return ((unsigned)s < VM_N_STATES) ? NAMES[s] : "?";
}

const char *vm_exit_name(ExitReason r)
{
    static const char *const NAMES[] = {"-",      "HALT",   "PROTECTION_FAULT", "INVALID_INSTRUCTION",
                                        "KILLED", "MAX_TICKS"};
    return NAMES[r];
}
