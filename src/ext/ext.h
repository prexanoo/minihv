#ifndef EXT_EXT_H
#define EXT_EXT_H

#include <stdbool.h>
#include <stdio.h>

#include "hv/events.h"

/* Recurso extra do escopo 09: GANTT. So o hv inclui este header.
 * O recurso recebe tudo por eventos (Observer), nenhum modulo chama
 * funcoes daqui diretamente (RF-EXT-02). */

typedef struct ExtConfig {
    bool enabled;
    int pcpus;
    const char *csv_file;
} ExtConfig;

typedef struct Ext Ext;

Ext *ext_create(const ExtConfig *cfg);
void ext_destroy(Ext *ext);

/* callback inscrito no hv */
void ext_on_event(void *ctx, const Event *ev);

/* comando do modo interativo; devolve false se o comando nao e do ext */
bool ext_command(Ext *ext, const char *cmd, FILE *out);
const char *ext_help(void);

/* fim da simulacao: imprime o diagrama e grava o csv */
void ext_finish(Ext *ext, FILE *out);

#endif
