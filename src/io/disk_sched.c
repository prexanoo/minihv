#include <string.h>

#include "io/device.h"

bool disk_algo_parse(const char *s, DiskAlgo *out)
{
    if (strcmp(s, "FCFS") == 0) {
        *out = DISK_FCFS;
        return true;
    }
    if (strcmp(s, "C_LOOK") == 0) {
        *out = DISK_C_LOOK;
        return true;
    }
    return false;
}

const char *disk_algo_name(DiskAlgo a)
{
    return a == DISK_FCFS ? "FCFS" : "C_LOOK";
}

static int cyl_at(const Queue *q, size_t i)
{
    const IoReq *r = queue_at(q, i);
    return r->arg;
}

/* C_LOOK: a cabeca so atende subindo. Pega o pedido mais proximo com
 * cilindro >= cabeca; se nao houver nenhum acima, salta direto para o
 * menor pendente (o salto conta na distancia) e continua subindo.
 * Empate no mesmo cilindro: o mais antigo, porque a fila esta em ordem
 * de chegada e so troca com < estrito. */
static size_t c_look(const Queue *q, int head, int *dist)
{
    size_t acima = 0, menor = 0;
    bool tem_acima = false;
    for (size_t i = 0; i < queue_size(q); i++) {
        int c = cyl_at(q, i);
        if (c >= head && (!tem_acima || c < cyl_at(q, acima))) {
            acima = i;
            tem_acima = true;
        }
        if (c < cyl_at(q, menor))
            menor = i;
    }
    if (tem_acima) {
        *dist = cyl_at(q, acima) - head;
        return acima;
    }
    *dist = head - cyl_at(q, menor);
    return menor;
}

size_t disk_pick(const Queue *q, DiskAlgo algo, int head, int *dist)
{
    if (algo == DISK_C_LOOK)
        return c_look(q, head, dist);
    int c = cyl_at(q, 0);
    *dist = c > head ? c - head : head - c;
    return 0;
}
