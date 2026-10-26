#ifndef UTIL_LIST_H
#define UTIL_LIST_H

#include <stdbool.h>
#include <stdlib.h>

/* Lista generica (TAD) baseada em vetor dinamico de ponteiros. */
typedef struct List List;

List *list_create(void);
void list_destroy(List *l);

bool list_append(List *l, void *item);
bool list_insert(List *l, size_t pos, void *item);
void *list_get(const List *l, size_t i);
void *list_remove_at(List *l, size_t i);
size_t list_size(const List *l);

#endif
