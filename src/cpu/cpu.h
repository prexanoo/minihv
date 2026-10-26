#ifndef CPU_CPU_H
#define CPU_CPU_H

#include <stdint.h>

#include "hv/events.h"
#include "io/io.h"
#include "mem/mem.h"
#include "vm/vm.h"

typedef enum CpuResult {
    CPU_OK,        /* executou e continua RUNNING */
    CPU_BLOCKED,   /* fez E/S */
    CPU_HALT,
    CPU_PROT_FAULT,
    CPU_BAD_INSTR
} CpuResult;

/* Conjunto das pCPUs do host. Cada pCPU tem seu proprio banco de
 * registradores; a troca de contexto copia VCB <-> pCPU. */
typedef struct Cpu Cpu;

Cpu *cpu_create(int n, const EventSink *sink);
void cpu_destroy(Cpu *cpu);

Vcb *cpu_current(const Cpu *cpu, int i);
int32_t cpu_pc(const Cpu *cpu, int i);

/* RF-CPU-03: restaura o contexto da VM na pCPU i */
void cpu_switch_in(Cpu *cpu, int i, Vcb *vm, uint64_t tick);
/* salva o contexto no VCB e libera a pCPU; devolve a VM que saiu */
Vcb *cpu_switch_out(Cpu *cpu, int i);

/* RF-CPU-01: executa uma instrucao (1 tick) da VM que esta na pCPU i */
CpuResult cpu_step(Cpu *cpu, int i, Mem *mem, Io *io, uint64_t tick);

uint64_t cpu_busy_ticks(const Cpu *cpu, int i);
uint64_t cpu_ctx_switches(const Cpu *cpu);

#endif
