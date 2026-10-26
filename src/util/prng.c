#include "util/prng.h"

#define PRNG_A 1103515245u
#define PRNG_C 12345u
#define PRNG_MASK 0x7FFFFFFFu /* mod 2^31 */

void prng_init(Prng *p, uint32_t seed)
{
    p->x = seed & PRNG_MASK;
}

uint32_t prng_next(Prng *p)
{
    /* conta em 64 bits para nao estourar antes do mod */
    uint64_t x = (uint64_t)PRNG_A * p->x + PRNG_C;
    p->x = (uint32_t)(x & PRNG_MASK);
    return p->x;
}
