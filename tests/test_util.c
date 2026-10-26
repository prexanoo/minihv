#include <assert.h>
#include <stdio.h>

#include "util/list.h"
#include "util/prng.h"
#include "util/queue.h"

static void test_queue_fifo(void)
{
    Queue *q = queue_create();
    int v[20];
    for (int i = 0; i < 20; i++) { /* passa da capacidade inicial (8) */
        v[i] = i;
        assert(queue_push(q, &v[i]));
    }
    assert(queue_size(q) == 20);
    for (int i = 0; i < 20; i++)
        assert(*(int *)queue_pop(q) == i);
    assert(queue_empty(q));
    assert(queue_pop(q) == NULL);
    queue_destroy(q);
}

static void test_queue_remove(void)
{
    Queue *q = queue_create();
    int v[5] = {0, 1, 2, 3, 4};
    for (int i = 0; i < 5; i++)
        queue_push(q, &v[i]);
    queue_pop(q); /* desloca o head para testar o vetor circular */
    queue_push(q, &v[0]);
    /* fila: 1 2 3 4 0 */
    assert(queue_remove_at(q, 1) == &v[2]);
    assert(queue_remove(q, &v[4]));
    assert(!queue_remove(q, &v[4]));
    assert(queue_size(q) == 3);
    assert(queue_at(q, 0) == &v[1]);
    assert(queue_at(q, 1) == &v[3]);
    assert(queue_at(q, 2) == &v[0]);
    queue_destroy(q);
}

static void test_list(void)
{
    List *l = list_create();
    int a = 1, b = 2, c = 3;
    list_append(l, &a);
    list_append(l, &c);
    list_insert(l, 1, &b);
    assert(list_size(l) == 3);
    assert(list_get(l, 1) == &b);
    assert(list_remove_at(l, 0) == &a);
    assert(list_get(l, 0) == &b);
    assert(list_get(l, 5) == NULL);
    list_destroy(l);
}

/* valor esperado calculado a mao:
 * (1103515245 * 2026 + 12345) mod 2^31 = 191421147 */
static void test_prng(void)
{
    Prng p, q;
    prng_init(&p, 2026);
    assert(prng_next(&p) == 191421147u);
    prng_init(&p, 2026);
    prng_init(&q, 2026);
    for (int i = 0; i < 100; i++) {
        uint32_t x = prng_next(&p);
        assert(x == prng_next(&q));
        assert(x < 0x80000000u);
    }
}

int main(void)
{
    test_queue_fifo();
    test_queue_remove();
    test_list();
    test_prng();
    puts("test_util: ok");
    return 0;
}
