#ifndef IO_DEVICE_H
#define IO_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "io/io.h"
#include "util/queue.h"

/* Estruturas internas do modulo io (nao usadas fora dele). */

typedef enum DiskAlgo { DISK_FCFS, DISK_C_LOOK } DiskAlgo;

typedef struct IoReq {
    Vcb *vm;          /* NULL se a VM foi morta durante o atendimento */
    int32_t arg;      /* valor (CONSOLE), cilindro (DISK) ou palavras (NET) */
    uint64_t enq_tick;
} IoReq;

/* Produtor/consumidor: as VMs produzem IoReq na fila e o dispositivo
 * consome uma por vez. */
typedef struct Device {
    DevId id;
    const char *name;
    Queue *queue;
    IoReq *cur;
    uint64_t finish;   /* tick em que a requisicao atual termina */
    uint64_t started;
    uint64_t served;
    uint64_t wait_sum;
} Device;

bool disk_algo_parse(const char *s, DiskAlgo *out);
const char *disk_algo_name(DiskAlgo a);

/* RF-IO-04: escolhe a proxima requisicao do disco. Devolve a posicao na
 * fila e, em *dist, quantos cilindros a cabeca anda ate ela. */
size_t disk_pick(const Queue *q, DiskAlgo algo, int head, int *dist);

#endif
