#ifndef UTIL_QUEUE_H
#define UTIL_QUEUE_H

#include <stdbool.h>
#include <stdlib.h>

/* Fila generica (TAD) de ponteiros, implementada como vetor circular
 * que cresce quando enche. Usada pelo sched e pelo io. */
typedef struct Queue Queue;

Queue *queue_create(void);
void queue_destroy(Queue *q);

bool queue_push(Queue *q, void *item);
void *queue_pop(Queue *q);
void *queue_peek(const Queue *q);
size_t queue_size(const Queue *q);
bool queue_empty(const Queue *q);

/* Acesso por posicao (0 = inicio da fila) e remocao no meio,
 * necessarios para politicas que nao pegam sempre o primeiro. */
void *queue_at(const Queue *q, size_t i);
void *queue_remove_at(Queue *q, size_t i);
bool queue_remove(Queue *q, const void *item);

#endif
