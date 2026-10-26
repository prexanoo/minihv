#ifndef HV_HV_H
#define HV_HV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hv/events.h"
#include "util/status.h"

/* Facade: unica API usada por main e cli. */

#define HV_ESCOPO 9

typedef struct HvOptions {
    const char *config_path;     /* NULL = conf/hv.conf */
    const char *policy_override; /* -p, NULL = usa o do arquivo */
    FILE *out;                   /* saida de console e relatorio (NULL = stdout) */
} HvOptions;

typedef struct Hv Hv;

/* RF-HV-01 */
HvStatus hv_create(const HvOptions *opt, Hv **out);
/* RF-HV-05 */
void hv_destroy(Hv *hv);

/* RF-HV-02: executa um tick; devolve false quando a simulacao acabou */
bool hv_step(Hv *hv);
/* roda ate n ticks (0 = ate o fim) */
void hv_run(Hv *hv, uint64_t n);
bool hv_finished(const Hv *hv);
uint64_t hv_clock(const Hv *hv);

/* RF-HV-03 / RF-LOG-04: relatorio final + saida do recurso extra */
void hv_print_report(const Hv *hv, FILE *out);
void hv_finish(Hv *hv, FILE *out);

/* consultas e comandos do modo interativo */
void hv_print_ps(const Hv *hv, FILE *out);
void hv_print_mem(const Hv *hv, FILE *out);
void hv_print_io(const Hv *hv, FILE *out);
HvStatus hv_pause(Hv *hv, const char *vm, char *msg, size_t n);
HvStatus hv_resume(Hv *hv, const char *vm, char *msg, size_t n);
HvStatus hv_kill(Hv *hv, const char *vm, char *msg, size_t n);
bool hv_ext_command(Hv *hv, const char *cmd, FILE *out);
const char *hv_ext_help(void);

/* inscreve mais um observador (usado pelos testes) */
HvStatus hv_subscribe(Hv *hv, EventFn fn, void *ctx);

#endif
