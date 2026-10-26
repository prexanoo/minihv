#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "log/log.h"
#include "util/status.h"

#define CFG_MAX_VMS 64
#define CFG_NAME_LEN 16
#define CFG_PATH_LEN 256
#define CFG_WORD_LEN 16
#define CFG_ERR_LEN 256

typedef struct VmConfig {
    char name[CFG_NAME_LEN];
    char program[CFG_PATH_LEN];
    int arrival;
    int priority;
    int mem_words;
    int line; /* linha do [vm], para mensagens de erro */
} VmConfig;

/* Valores ja validados do hv.conf. Struct simples (sem tipo opaco) porque
 * e so um pacote de parametros lido por varios modulos na inicializacao. */
typedef struct HvConfig {
    char path[CFG_PATH_LEN];
    /* [host] */
    int pcpus;
    int max_vms;
    long max_ticks;
    uint32_t seed;
    /* [sched] */
    char policy[CFG_WORD_LEN];
    int quantum;
    /* [mem] */
    char mem_model[CFG_WORD_LEN];
    int phys_words;
    int page_words;
    char allocation[CFG_WORD_LEN];
    char replacement[CFG_WORD_LEN];
    char repl_scope[CFG_WORD_LEN];
    int pf_penalty;
    /* [io] */
    char disk_algorithm[CFG_WORD_LEN];
    int disk_cylinders;
    int disk_head;
    int seek_rate;
    /* [log] */
    LogFormat log_format;
    LogLevel log_level;
    char log_file[CFG_PATH_LEN];
    /* [ext] */
    bool ext_enabled;
    char ext_file[CFG_PATH_LEN];
    /* [vm] ... */
    VmConfig vms[CFG_MAX_VMS];
    int n_vms;
} HvConfig;

/* RF-CFG-05: valores padrao do escopo 09 */
void config_defaults(HvConfig *cfg);

/* RF-CFG-01: le o arquivo. Em erro, err recebe "arquivo:linha: mensagem". */
HvStatus config_load(HvConfig *cfg, const char *path, char *err, size_t errlen);

/* Mesmo parser, mas a partir de um texto em memoria (usado nos testes). */
HvStatus config_parse_text(HvConfig *cfg, const char *name, const char *text, char *err,
                           size_t errlen);

#endif
