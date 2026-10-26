#ifndef UTIL_PRNG_H
#define UTIL_PRNG_H

#include <stdint.h>

/* LCG fixo do enunciado: x = (1103515245 * x + 12345) mod 2^31.
 * Nenhuma politica do escopo 09 sorteia nada, mas o gerador fica
 * disponivel e e o unico jeito permitido de ter aleatoriedade. */
typedef struct Prng {
    uint32_t x;
} Prng;

void prng_init(Prng *p, uint32_t seed);
uint32_t prng_next(Prng *p);

#endif
