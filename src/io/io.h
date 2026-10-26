#ifndef IO_IO_H
#define IO_IO_H

#include <stdint.h>
#include <stdio.h>

#include "hv/events.h"
#include "util/status.h"
#include "vm/vm.h"

typedef enum DevId { DEV_CONSOLE = 0, DEV_DISK = 1, DEV_NET = 2, DEV_COUNT } DevId;

/* callback de interrupcao, registrado pelo hv (o io nao chama ninguem acima) */
typedef void (*IoDoneFn)(void *ctx, Vcb *vm, DevId dev, uint64_t tick);

typedef struct IoConfig {
    const char *disk_algorithm;
    int disk_cylinders;
    int disk_head;
    int seek_rate;
    FILE *console;          /* onde o CONSOLE imprime (stdout no uso normal) */
    const EventSink *sink;
    IoDoneFn on_done;
    void *done_ctx;
} IoConfig;

typedef struct Io Io;

Io *io_create(const IoConfig *cfg);
void io_destroy(Io *io);

/* RF-IO-02: enfileira a requisicao; quem bloqueia a VM e o hv */
HvStatus io_request(Io *io, Vcb *vm, DevId dev, int32_t arg, uint64_t tick);

/* passo 2 do tick: conclui requisicoes (interrupcoes) e inicia as proximas */
void io_tick(Io *io, uint64_t tick);

/* descarta requisicoes de uma VM encerrada (kill) */
void io_cancel_vm(Io *io, const Vcb *vm);

const char *io_disk_algorithm(const Io *io);
void io_print(const Io *io, FILE *out);
void io_report_lines(const Io *io, char *buf, size_t n);

#endif
