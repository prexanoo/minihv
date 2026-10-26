#ifndef MEM_MEM_POLICY_H
#define MEM_MEM_POLICY_H

/* Strategy do algoritmo de alocacao contigua. Recebe os buracos em ordem
 * crescente de endereco e devolve o indice do escolhido (-1 = nenhum cabe). */

typedef struct Hole {
    int base;
    int size;
} Hole;

typedef struct AllocPolicy {
    const char *name;
    int (*choose)(const Hole *holes, int n, int size);
} AllocPolicy;

const AllocPolicy *mem_policy_create(const char *name); /* Factory */

extern const AllocPolicy ALLOC_BEST_FIT;

#endif
