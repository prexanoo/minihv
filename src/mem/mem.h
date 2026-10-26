#ifndef MEM_MEM_H
#define MEM_MEM_H

#include <stdint.h>
#include <stdio.h>

#include "hv/events.h"
#include "util/status.h"
#include "vm/vm.h"

/* Gerencia de memoria do escopo 09: modelo CONT (particoes variaveis),
 * sem paginacao e sem dispositivo SWAP. */

typedef struct MemConfig {
    int phys_words;
    const char *allocation; /* nome do algoritmo, ex.: "BEST_FIT" */
    const EventSink *sink;
} MemConfig;

typedef struct Mem Mem;

Mem *mem_create(const MemConfig *cfg);
void mem_destroy(Mem *mem);

/* RF-MEM-06/07: reserva a particao da VM. Se precisar compactar, faz a
 * compactacao e devolve em *moved quantas particoes mudaram de lugar
 * (o hv congela as VMs por esse numero de ticks). Sem espaco: HV_ERR_NOSPACE. */
HvStatus mem_alloc(Mem *mem, Vcb *vm, uint64_t tick, int *moved);

/* RF-MEM-08 */
void mem_release(Mem *mem, Vcb *vm, uint64_t tick);

/* traducao por base e limite; fora do espaco da VM -> HV_ERR_PROTECTION */
HvStatus mem_translate(const Mem *mem, const Vcb *vm, int32_t vaddr, int *paddr);
HvStatus mem_read(const Mem *mem, const Vcb *vm, int32_t vaddr, int32_t *val);
HvStatus mem_write(Mem *mem, const Vcb *vm, int32_t vaddr, int32_t val);

/* RF-MEM-09: amostra ocupacao e fragmentacao uma vez por tick */
void mem_sample(Mem *mem);

int mem_free_words(const Mem *mem);
int mem_largest_hole(const Mem *mem);
int mem_compactions(const Mem *mem);
const char *mem_policy_name(const Mem *mem);

void mem_print(const Mem *mem, FILE *out);
void mem_report_line(const Mem *mem, char *buf, size_t n);

#endif
