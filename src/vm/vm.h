#ifndef VM_VM_H
#define VM_VM_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/loader.h"
#include "hv/events.h"
#include "util/status.h"

typedef enum VmState {
    VM_NEW,
    VM_READY,
    VM_RUNNING,
    VM_BLOCKED,
    VM_PAUSED,
    VM_TERMINATED,
    VM_N_STATES
} VmState;

typedef enum ExitReason {
    EXIT_NONE,
    EXIT_HALT,
    EXIT_PROTECTION_FAULT,
    EXIT_INVALID_INSTRUCTION,
    EXIT_KILLED,
    EXIT_MAX_TICKS
} ExitReason;

/* VCB: equivalente ao PCB. Tipo opaco, acesso so pelas funcoes abaixo. */
typedef struct Vcb Vcb;

typedef struct VmParams {
    int id;
    const char *name;
    int priority;
    int mem_words;
    uint64_t arrival;
} VmParams;

/* RF-VM-01/04: cria o VCB em NEW. O VCB passa a ser dono do programa. */
Vcb *vm_create(const VmParams *p, Program *prog, const EventSink *sink, uint64_t tick);
void vm_destroy(Vcb *vm);

int vm_id(const Vcb *vm);
const char *vm_name(const Vcb *vm);
VmState vm_state(const Vcb *vm);
int vm_priority(const Vcb *vm);
int vm_mem_words(const Vcb *vm);
uint64_t vm_arrival(const Vcb *vm);
const Program *vm_program(const Vcb *vm);
ExitReason vm_exit_reason(const Vcb *vm);
bool vm_is_admitted(const Vcb *vm); /* fora de NEW e de TERMINATED */

/* registradores salvos (vCPU) */
void vm_get_context(const Vcb *vm, int32_t regs[MV_NREGS], int32_t *pc);
void vm_set_context(Vcb *vm, const int32_t regs[MV_NREGS], int32_t pc);

/* particao no modelo CONT: registradores base e limite */
void vm_set_partition(Vcb *vm, int base, int limit);
int vm_base(const Vcb *vm);
int vm_limit(const Vcb *vm);

/* RF-VM-02: unica porta de troca de estado */
HvStatus vm_set_state(Vcb *vm, VmState to, uint64_t tick);
HvStatus vm_admit(Vcb *vm, uint64_t tick);
HvStatus vm_terminate(Vcb *vm, ExitReason why, uint64_t tick);

/* contadores de estatistica */
void vm_on_dispatch(Vcb *vm, uint64_t tick);
void vm_count_ready_tick(Vcb *vm);
void vm_count_cpu_tick(Vcb *vm);
void vm_count_io(Vcb *vm);
uint64_t vm_wait_ticks(const Vcb *vm);
uint64_t vm_cpu_ticks(const Vcb *vm);
uint64_t vm_io_count(const Vcb *vm);
bool vm_was_dispatched(const Vcb *vm);
uint64_t vm_first_dispatch(const Vcb *vm);
uint64_t vm_end_tick(const Vcb *vm);

const char *vm_state_name(VmState s);
const char *vm_exit_name(ExitReason r);

#endif
