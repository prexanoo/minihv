#include "util/list.h"

#include <stdlib.h>
#include <string.h>

#define LIST_INIT_CAP 8

struct List {
    void **items;
    size_t size;
    size_t cap;
};

List *list_create(void)
{
    List *l = malloc(sizeof *l);
    if (!l)
        return NULL;
    l->items = malloc(LIST_INIT_CAP * sizeof *l->items);
    if (!l->items) {
        free(l);
        return NULL;
    }
    l->size = 0;
    l->cap = LIST_INIT_CAP;
    return l;
}

void list_destroy(List *l)
{
    if (!l)
        return;
    free(l->items);
    free(l);
}

static bool list_reserve(List *l)
{
    if (l->size < l->cap)
        return true;
    void **novo = realloc(l->items, l->cap * 2 * sizeof *novo);
    if (!novo)
        return false;
    l->items = novo;
    l->cap *= 2;
    return true;
}

bool list_append(List *l, void *item)
{
    return list_insert(l, l->size, item);
}

bool list_insert(List *l, size_t pos, void *item)
{
    if (pos > l->size || !list_reserve(l))
        return false;
    memmove(&l->items[pos + 1], &l->items[pos], (l->size - pos) * sizeof *l->items);
    l->items[pos] = item;
    l->size++;
    return true;
}

void *list_get(const List *l, size_t i)
{
    return i < l->size ? l->items[i] : NULL;
}

void *list_remove_at(List *l, size_t i)
{
    if (i >= l->size)
        return NULL;
    void *item = l->items[i];
    memmove(&l->items[i], &l->items[i + 1], (l->size - i - 1) * sizeof *l->items);
    l->size--;
    return item;
}

size_t list_size(const List *l)
{
    return l->size;
}
