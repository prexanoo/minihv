#ifndef HV_EVENTS_H
#define HV_EVENTS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log/log.h"

/* Contrato do padrao Observer. Os modulos de baixo recebem um EventSink
 * (ponteiro de funcao + contexto) e nao precisam incluir hv.h, o que
 * evita dependencia circular. Quem distribui para os inscritos e o hv. */

typedef enum EventType {
    EV_VM_CREATE,
    EV_VM_ADMIT,
    EV_STATE_CHANGE,
    EV_DISPATCH,
    EV_PREEMPT,
    EV_CTX_SWITCH,
    EV_MEM_ALLOC,
    EV_MEM_FREE,
    EV_COMPACT,
    EV_IO_REQ,
    EV_IO_DONE,
    EV_VM_EXIT,
    EV_CREDIT,
    EV_VM_REJECT,
    /* internos: nao vao para o arquivo de log, servem ao ext */
    EV_EXEC,
    EV_TICK_END
} EventType;

#define EVENT_DETAIL_LEN 128

typedef struct Event {
    EventType type;
    LogLevel level;
    const char *module;
    uint64_t tick;
    int vm_id;          /* -1 quando nao se aplica */
    const char *vm;     /* nome da VM ou NULL */
    int cpu;            /* -1 quando nao se aplica */
    char detail[EVENT_DETAIL_LEN];
} Event;

typedef void (*EventFn)(void *ctx, const Event *ev);

typedef struct EventSink {
    EventFn emit;
    void *ctx;
} EventSink;

static inline Event event_make(EventType type, const char *module, uint64_t tick)
{
    Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.level = LOG_INFO;
    ev.module = module;
    ev.tick = tick;
    ev.vm_id = -1;
    ev.cpu = -1;
    return ev;
}

static inline void event_emit(const EventSink *sink, const Event *ev)
{
    if (sink && sink->emit)
        sink->emit(sink->ctx, ev);
}

static inline const char *event_name(EventType t)
{
    static const char *const NAMES[] = {
        "VM_CREATE", "VM_ADMIT",   "STATE_CHANGE", "DISPATCH",  "PREEMPT",
        "CTX_SWITCH", "MEM_ALLOC", "MEM_FREE",     "COMPACT",   "IO_REQ",
        "IO_DONE",   "VM_EXIT",    "CREDIT",       "VM_REJECT", "EXEC",
        "TICK_END"};
    return NAMES[t];
}

static inline int event_is_internal(EventType t)
{
    return t == EV_EXEC || t == EV_TICK_END;
}

#endif
