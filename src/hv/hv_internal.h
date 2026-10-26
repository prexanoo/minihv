#ifndef HV_HV_INTERNAL_H
#define HV_HV_INTERNAL_H

/* Uso interno do modulo hv: so hv.c e hv_cmd.c incluem este arquivo.
 * Para o resto do programa o Hv continua opaco (so hv.h). */

#include "hv/hv.h"

#include "config/config.h"
#include "cpu/cpu.h"
#include "ext/ext.h"
#include "io/io.h"
#include "mem/mem.h"
#include "sched/sched.h"
#include "vm/vm.h"

#define MAX_SUBS 8

typedef struct Subscriber {
    EventFn fn;
    void *ctx;
} Subscriber;

struct Hv {
    HvConfig cfg;
    Logger *log;
    Mem *mem;
    Io *io;
    Scheduler *sched;
    Cpu *cpu;
    Ext *ext;
    Vcb **vms;     /* em ordem de id */
    int n_vms;
    Vcb **order;   /* em ordem de chegada (arrival, id), usada na admissao */
    Vcb **pending; /* ficaram prontas neste tick; entram na fila por id */
    int n_pending;
    bool *preempt; /* preempcao pendente em cada pCPU */
    uint64_t clock;
    int stall;     /* ticks restantes de compactacao (VMs congeladas) */
    bool finished;
    Subscriber subs[MAX_SUBS];
    int n_subs;
    EventSink sink;
    FILE *out;
};

/* pCPU onde a VM esta rodando, ou -1 */
int hv_find_cpu(const Hv *hv, const Vcb *vm);
/* tira a VM de onde estiver e encerra (HALT, falta, kill ou max_ticks) */
void hv_stop_vm(Hv *hv, Vcb *vm, ExitReason why);
/* RF-HV-03: marca o fim quando todas terminaram ou chegou em max_ticks */
void hv_check_end(Hv *hv);

#endif
