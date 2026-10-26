/* BEST_FIT: o menor buraco que cabe; empate fica com o de menor endereco
 * (os buracos chegam ordenados, entao basta usar < e nao <=). */
#include <string.h>

#include "mem/mem_policy.h"

static int best_fit_choose(const Hole *holes, int n, int size)
{
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (holes[i].size < size)
            continue;
        if (best < 0 || holes[i].size < holes[best].size)
            best = i;
    }
    return best;
}

const AllocPolicy ALLOC_BEST_FIT = {"BEST_FIT", best_fit_choose};

const AllocPolicy *mem_policy_create(const char *name)
{
    if (strcmp(name, ALLOC_BEST_FIT.name) == 0)
        return &ALLOC_BEST_FIT;
    return NULL;
}
