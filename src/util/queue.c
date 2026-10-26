#include "util/queue.h"

#include <stdlib.h>

#define QUEUE_INIT_CAP 8

struct Queue {
    void **items;
    size_t cap;
    size_t head;
    size_t size;
};

Queue *queue_create(void)
{
    Queue *q = malloc(sizeof *q);
    if (!q)
        return NULL;
    q->items = malloc(QUEUE_INIT_CAP * sizeof *q->items);
    if (!q->items) {
        free(q);
        return NULL;
    }
    q->cap = QUEUE_INIT_CAP;
    q->head = 0;
    q->size = 0;
    return q;
}

void queue_destroy(Queue *q)
{
    if (!q)
        return;
    free(q->items);
    free(q);
}

/* dobra a capacidade e reorganiza os itens a partir do indice 0 */
static bool queue_grow(Queue *q)
{
    size_t new_cap = q->cap * 2;
    void **novo = malloc(new_cap * sizeof *novo);
    if (!novo)
        return false;
    for (size_t i = 0; i < q->size; i++)
        novo[i] = q->items[(q->head + i) % q->cap];
    free(q->items);
    q->items = novo;
    q->cap = new_cap;
    q->head = 0;
    return true;
}

bool queue_push(Queue *q, void *item)
{
    if (q->size == q->cap && !queue_grow(q))
        return false;
    q->items[(q->head + q->size) % q->cap] = item;
    q->size++;
    return true;
}

void *queue_pop(Queue *q)
{
    if (q->size == 0)
        return NULL;
    void *item = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->size--;
    return item;
}

void *queue_peek(const Queue *q)
{
    return q->size ? q->items[q->head] : NULL;
}

size_t queue_size(const Queue *q)
{
    return q->size;
}

bool queue_empty(const Queue *q)
{
    return q->size == 0;
}

void *queue_at(const Queue *q, size_t i)
{
    if (i >= q->size)
        return NULL;
    return q->items[(q->head + i) % q->cap];
}

void *queue_remove_at(Queue *q, size_t i)
{
    if (i >= q->size)
        return NULL;
    void *item = queue_at(q, i);
    /* puxa os itens seguintes uma posicao para tras */
    for (size_t k = i; k + 1 < q->size; k++)
        q->items[(q->head + k) % q->cap] = q->items[(q->head + k + 1) % q->cap];
    q->size--;
    return item;
}

bool queue_remove(Queue *q, const void *item)
{
    for (size_t i = 0; i < q->size; i++) {
        if (queue_at(q, i) == item) {
            queue_remove_at(q, i);
            return true;
        }
    }
    return false;
}
