#include "io/io.h"

#include <stdlib.h>
#include <string.h>

#include "io/device.h"
#include "log/stats.h"

#define LAT_CONSOLE 2
#define LAT_NET_BASE 2
#define NET_WORDS_PER_TICK 8
#define LAT_DISK_BASE 1

struct Io {
    Device dev[DEV_COUNT];
    DiskAlgo algo;
    int cylinders;
    int head;
    int seek_rate;
    uint64_t head_moves;
    FILE *console;
    const EventSink *sink;
    IoDoneFn on_done;
    void *done_ctx;
};

static const char *const DEV_NAMES[DEV_COUNT] = {"CONSOLE", "DISK", "NET"};

Io *io_create(const IoConfig *cfg)
{
    Io *io = calloc(1, sizeof *io);
    if (!io)
        return NULL;
    if (!disk_algo_parse(cfg->disk_algorithm, &io->algo)) {
        free(io);
        return NULL;
    }
    for (int i = 0; i < DEV_COUNT; i++) {
        io->dev[i].id = (DevId)i;
        io->dev[i].name = DEV_NAMES[i];
        io->dev[i].queue = queue_create();
        if (!io->dev[i].queue) {
            io_destroy(io);
            return NULL;
        }
    }
    io->cylinders = cfg->disk_cylinders;
    io->head = cfg->disk_head;
    io->seek_rate = cfg->seek_rate;
    io->console = cfg->console ? cfg->console : stdout;
    io->sink = cfg->sink;
    io->on_done = cfg->on_done;
    io->done_ctx = cfg->done_ctx;
    return io;
}

void io_destroy(Io *io)
{
    if (!io)
        return;
    for (int i = 0; i < DEV_COUNT; i++) {
        Queue *q = io->dev[i].queue;
        while (q && !queue_empty(q))
            free(queue_pop(q));
        queue_destroy(q);
        free(io->dev[i].cur);
    }
    free(io);
}

static void emit(const Io *io, EventType t, const Vcb *vm, uint64_t tick, const char *det)
{
    Event ev = event_make(t, "io", tick);
    if (vm) {
        ev.vm_id = vm_id(vm);
        ev.vm = vm_name(vm);
    }
    snprintf(ev.detail, sizeof ev.detail, "%s", det);
    event_emit(io->sink, &ev);
}

static int ceil_div(int a, int b)
{
    return (a + b - 1) / b;
}

HvStatus io_request(Io *io, Vcb *vm, DevId dev, int32_t arg, uint64_t tick)
{
    IoReq *r = malloc(sizeof *r);
    if (!r)
        return HV_ERR_NOMEM;
    if (dev == DEV_DISK) /* RF-IO-04: cilindro = Rs mod disk_cylinders (sempre >= 0) */
        arg = ((arg % io->cylinders) + io->cylinders) % io->cylinders;
    r->vm = vm;
    r->arg = arg;
    r->enq_tick = tick;
    if (!queue_push(io->dev[dev].queue, r)) {
        free(r);
        return HV_ERR_NOMEM;
    }
    static const char *const ARG[DEV_COUNT] = {"value", "cyl", "words"};
    char det[EVENT_DETAIL_LEN];
    snprintf(det, sizeof det, "dev=%s %s=%d", DEV_NAMES[dev], ARG[dev], (int)arg);
    emit(io, EV_IO_REQ, vm, tick, det);
    return HV_OK;
}

static void complete(Io *io, Device *d, uint64_t tick)
{
    IoReq *r = d->cur;
    d->cur = NULL;
    d->served++;
    if (r->vm) {
        if (d->id == DEV_CONSOLE) /* RF-IO-05 */
            fprintf(io->console, "[t=%04llu] %s: %d\n", (unsigned long long)tick,
                    vm_name(r->vm), (int)r->arg);
        char det[EVENT_DETAIL_LEN];
        snprintf(det, sizeof det, "dev=%s arg=%d", d->name, (int)r->arg);
        emit(io, EV_IO_DONE, r->vm, tick, det);
        io->on_done(io->done_ctx, r->vm, d->id, tick); /* interrupcao */
    }
    free(r);
}

/* RF-IO-03: latencias de cada dispositivo */
static int start_latency(Io *io, Device *d, size_t *pos)
{
    *pos = 0;
    const IoReq *first = queue_peek(d->queue);
    switch (d->id) {
    case DEV_CONSOLE:
        return LAT_CONSOLE;
    case DEV_NET:
        return LAT_NET_BASE + ceil_div(first->arg, NET_WORDS_PER_TICK);
    case DEV_DISK: {
        int dist = 0;
        *pos = disk_pick(d->queue, io->algo, io->head, &dist);
        const IoReq *r = queue_at(d->queue, *pos);
        io->head = r->arg;
        io->head_moves += (uint64_t)dist;
        return LAT_DISK_BASE + ceil_div(dist, io->seek_rate);
    }
    case DEV_COUNT:
        break;
    }
    return 1;
}

static void start(Io *io, Device *d, uint64_t tick)
{
    size_t pos;
    int lat = start_latency(io, d, &pos);
    d->cur = queue_remove_at(d->queue, pos);
    d->finish = tick + (uint64_t)lat;
    d->started++;
    d->wait_sum += tick - d->cur->enq_tick;
}

/* A requisicao que comeca no passo 2 do tick s com latencia L ocupa os
 * ticks s..s+L-1 e a interrupcao e tratada no passo 2 do tick s+L. */
void io_tick(Io *io, uint64_t tick)
{
    for (int i = 0; i < DEV_COUNT; i++) {
        Device *d = &io->dev[i];
        if (d->cur && d->finish <= tick)
            complete(io, d, tick);
        if (!d->cur && !queue_empty(d->queue))
            start(io, d, tick);
    }
}

void io_cancel_vm(Io *io, const Vcb *vm)
{
    for (int i = 0; i < DEV_COUNT; i++) {
        Device *d = &io->dev[i];
        for (size_t k = 0; k < queue_size(d->queue);) {
            IoReq *r = queue_at(d->queue, k);
            if (r->vm == vm) {
                queue_remove_at(d->queue, k);
                free(r);
            } else {
                k++;
            }
        }
        if (d->cur && d->cur->vm == vm)
            d->cur->vm = NULL; /* o dispositivo termina, mas ninguem e acordado */
    }
}

const char *io_disk_algorithm(const Io *io)
{
    return disk_algo_name(io->algo);
}

void io_print(const Io *io, FILE *out)
{
    fprintf(out, "disco %s: cabeca no cilindro %d, deslocamento total=%llu\n",
            disk_algo_name(io->algo), io->head, (unsigned long long)io->head_moves);
    for (int i = 0; i < DEV_COUNT; i++) {
        const Device *d = &io->dev[i];
        fprintf(out, "  %-8s ", d->name);
        if (d->cur)
            fprintf(out, "atendendo %s (arg=%d, termina t=%llu)", d->cur->vm ? vm_name(d->cur->vm) : "-",
                    (int)d->cur->arg, (unsigned long long)d->finish);
        else
            fputs("livre", out);
        fputs(" | fila:", out);
        for (size_t k = 0; k < queue_size(d->queue); k++) {
            const IoReq *r = queue_at(d->queue, k);
            fprintf(out, " %s(%d)", vm_name(r->vm), (int)r->arg);
        }
        fprintf(out, " | atendidas=%llu\n", (unsigned long long)d->served);
    }
}

static double mean_wait(const Device *d)
{
    return d->started ? (double)d->wait_sum / (double)d->started : 0.0;
}

void io_report_lines(const Io *io, char *buf, size_t n)
{
    char e0[32], e1[32], e2[32];
    stats_fmt(e0, sizeof e0, mean_wait(&io->dev[DEV_CONSOLE]), 1);
    stats_fmt(e1, sizeof e1, mean_wait(&io->dev[DEV_DISK]), 1);
    stats_fmt(e2, sizeof e2, mean_wait(&io->dev[DEV_NET]), 1);
    snprintf(buf, n,
             "Disco: requisições=%llu  deslocamento_total=%llu cilindros  espera_média=%s\n"
             "Console: requisições=%llu  espera_média=%s\n"
             "Rede: requisições=%llu  espera_média=%s\n",
             (unsigned long long)io->dev[DEV_DISK].served, (unsigned long long)io->head_moves, e1,
             (unsigned long long)io->dev[DEV_CONSOLE].served, e0,
             (unsigned long long)io->dev[DEV_NET].served, e2);
}
