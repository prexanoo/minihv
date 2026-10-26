#include "config/config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LINE_MAX_LEN 512
#define MSG_LEN 160

typedef enum Section { SEC_NONE, SEC_HOST, SEC_SCHED, SEC_MEM, SEC_IO, SEC_LOG, SEC_EXT, SEC_VM } Section;

typedef struct Parser {
    HvConfig *cfg;
    const char *name;
    int line;
    Section sec;
    bool log_file_set;
    char *err;
    size_t errlen;
} Parser;

static const char *const POLICIES[] = {"CREDIT", "FCFS", NULL};
static const char *const MODELS[] = {"CONT", NULL};
static const char *const ALLOCS[] = {"BEST_FIT", NULL};
static const char *const REPLS[] = {"FIFO", "LRU", "CLOCK", "NRU", "LFU", NULL};
static const char *const SCOPES[] = {"LOCAL", "GLOBAL", NULL};
static const char *const DISKS[] = {"C_LOOK", "FCFS", NULL};

void config_defaults(HvConfig *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->pcpus = 4;
    cfg->max_vms = 6;
    cfg->max_ticks = 10000;
    cfg->seed = 2026;
    strcpy(cfg->policy, "CREDIT");
    cfg->quantum = 5;
    strcpy(cfg->mem_model, "CONT");
    cfg->phys_words = 4096;
    strcpy(cfg->allocation, "BEST_FIT");
    cfg->pf_penalty = 3;
    strcpy(cfg->disk_algorithm, "C_LOOK");
    cfg->disk_cylinders = 200;
    cfg->disk_head = 50;
    cfg->seek_rate = 25;
    cfg->log_format = LOG_FMT_JSONL;
    cfg->log_level = LOG_INFO;
    strcpy(cfg->log_file, log_default_file(LOG_FMT_JSONL));
    cfg->ext_enabled = true;
    strcpy(cfg->ext_file, "out/gantt.csv");
}

static HvStatus fail(Parser *p, const char *msg)
{
    if (p->line > 0)
        snprintf(p->err, p->errlen, "%s:%d: %s", p->name, p->line, msg);
    else
        snprintf(p->err, p->errlen, "%s: %s", p->name, msg);
    return HV_ERR_PARSE;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static HvStatus parse_long(Parser *p, const char *key, const char *val, long min, long max,
                           long *out)
{
    char *end;
    long v = strtol(val, &end, 10);
    char msg[MSG_LEN];
    if (*val == '\0' || *end != '\0') {
        snprintf(msg, sizeof msg, "%s deve ser um numero inteiro", key);
        return fail(p, msg);
    }
    if (v < min || v > max) {
        snprintf(msg, sizeof msg, "%s deve estar entre %ld e %ld", key, min, max);
        return fail(p, msg);
    }
    *out = v;
    return HV_OK;
}

static HvStatus parse_int(Parser *p, const char *key, const char *val, long min, long max,
                          int *out)
{
    long v;
    HvStatus st = parse_long(p, key, val, min, max, &v);
    if (st == HV_OK)
        *out = (int)v;
    return st;
}

static HvStatus parse_choice(Parser *p, const char *key, const char *val,
                             const char *const opts[], char *dst)
{
    for (int i = 0; opts[i]; i++) {
        if (strcmp(val, opts[i]) == 0) {
            strcpy(dst, opts[i]);
            return HV_OK;
        }
    }
    char msg[MSG_LEN];
    int n = snprintf(msg, sizeof msg, "%s invalido: '%s' (aceitos:", key, val);
    for (int i = 0; opts[i] && n < (int)sizeof msg; i++)
        n += snprintf(msg + n, sizeof msg - (size_t)n, " %s", opts[i]);
    if (n < (int)sizeof msg)
        snprintf(msg + n, sizeof msg - (size_t)n, ")");
    return fail(p, msg);
}

static HvStatus parse_string(Parser *p, const char *key, const char *val, char *dst,
                             size_t cap)
{
    char msg[MSG_LEN];
    if (*val == '\0' || strlen(val) >= cap) {
        snprintf(msg, sizeof msg, "%s vazio ou longo demais", key);
        return fail(p, msg);
    }
    strcpy(dst, val);
    return HV_OK;
}

static HvStatus parse_bool(Parser *p, const char *key, const char *val, bool *out)
{
    if (strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
        *out = true;
        return HV_OK;
    }
    if (strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
        *out = false;
        return HV_OK;
    }
    char msg[MSG_LEN];
    snprintf(msg, sizeof msg, "%s deve ser true ou false", key);
    return fail(p, msg);
}

static HvStatus unknown_key(Parser *p, const char *key)
{
    char msg[MSG_LEN];
    snprintf(msg, sizeof msg, "chave desconhecida '%s'", key);
    return fail(p, msg);
}

static HvStatus key_host(Parser *p, const char *k, const char *v)
{
    HvConfig *c = p->cfg;
    if (strcmp(k, "pcpus") == 0)
        return parse_int(p, k, v, 1, 16, &c->pcpus);
    if (strcmp(k, "max_vms") == 0)
        return parse_int(p, k, v, 1, CFG_MAX_VMS, &c->max_vms);
    if (strcmp(k, "max_ticks") == 0)
        return parse_long(p, k, v, 1, 10000000L, &c->max_ticks);
    if (strcmp(k, "seed") == 0) {
        long s;
        HvStatus st = parse_long(p, k, v, 0, 2147483647L, &s);
        c->seed = (uint32_t)s;
        return st;
    }
    return unknown_key(p, k);
}

static HvStatus key_sched(Parser *p, const char *k, const char *v)
{
    if (strcmp(k, "policy") == 0)
        return parse_choice(p, k, v, POLICIES, p->cfg->policy);
    if (strcmp(k, "quantum") == 0)
        return parse_int(p, k, v, 1, 50, &p->cfg->quantum);
    return unknown_key(p, k);
}

/* page_words, replacement e repl_scope so valem no modelo PAG; sao
 * aceitos para o arquivo ficar compativel, mas ignorados no CONT */
static HvStatus key_mem(Parser *p, const char *k, const char *v)
{
    HvConfig *c = p->cfg;
    if (strcmp(k, "model") == 0)
        return parse_choice(p, k, v, MODELS, c->mem_model);
    if (strcmp(k, "phys_words") == 0)
        return parse_int(p, k, v, 16, 1048576L, &c->phys_words);
    if (strcmp(k, "page_words") == 0)
        return parse_int(p, k, v, 1, 4096, &c->page_words);
    if (strcmp(k, "allocation") == 0)
        return parse_choice(p, k, v, ALLOCS, c->allocation);
    if (strcmp(k, "replacement") == 0)
        return parse_choice(p, k, v, REPLS, c->replacement);
    if (strcmp(k, "repl_scope") == 0)
        return parse_choice(p, k, v, SCOPES, c->repl_scope);
    if (strcmp(k, "pf_penalty") == 0)
        return parse_int(p, k, v, 1, 100, &c->pf_penalty);
    return unknown_key(p, k);
}

static HvStatus key_io(Parser *p, const char *k, const char *v)
{
    HvConfig *c = p->cfg;
    if (strcmp(k, "disk_algorithm") == 0)
        return parse_choice(p, k, v, DISKS, c->disk_algorithm);
    if (strcmp(k, "disk_cylinders") == 0)
        return parse_int(p, k, v, 1, 10000, &c->disk_cylinders);
    if (strcmp(k, "disk_head") == 0)
        return parse_int(p, k, v, 0, 9999, &c->disk_head);
    if (strcmp(k, "seek_rate") == 0)
        return parse_int(p, k, v, 1, 1000, &c->seek_rate);
    return unknown_key(p, k);
}

static HvStatus key_log(Parser *p, const char *k, const char *v)
{
    HvConfig *c = p->cfg;
    if (strcmp(k, "format") == 0) {
        if (!log_format_parse(v, &c->log_format))
            return fail(p, "format invalido (aceitos: TXT CSV JSONL)");
        return HV_OK;
    }
    if (strcmp(k, "level") == 0) {
        if (!log_level_parse(v, &c->log_level))
            return fail(p, "level invalido (aceitos: DEBUG INFO WARN ERROR)");
        return HV_OK;
    }
    if (strcmp(k, "file") == 0) {
        p->log_file_set = true;
        return parse_string(p, k, v, c->log_file, sizeof c->log_file);
    }
    return unknown_key(p, k);
}

static HvStatus key_ext(Parser *p, const char *k, const char *v)
{
    if (strcmp(k, "enabled") == 0)
        return parse_bool(p, k, v, &p->cfg->ext_enabled);
    if (strcmp(k, "file") == 0)
        return parse_string(p, k, v, p->cfg->ext_file, sizeof p->cfg->ext_file);
    return unknown_key(p, k);
}

static bool valid_vm_name(const char *s)
{
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
            return false;
    return true;
}

static HvStatus key_vm(Parser *p, const char *k, const char *v)
{
    VmConfig *vm = &p->cfg->vms[p->cfg->n_vms - 1];
    if (strcmp(k, "name") == 0) {
        if (!valid_vm_name(v))
            return fail(p, "name so pode ter letras, numeros, '_' e '-'");
        return parse_string(p, k, v, vm->name, sizeof vm->name);
    }
    if (strcmp(k, "program") == 0)
        return parse_string(p, k, v, vm->program, sizeof vm->program);
    if (strcmp(k, "arrival") == 0)
        return parse_int(p, k, v, 0, 10000000L, &vm->arrival);
    if (strcmp(k, "priority") == 0)
        return parse_int(p, k, v, 0, 9, &vm->priority);
    if (strcmp(k, "mem_words") == 0)
        return parse_int(p, k, v, 1, 1048576L, &vm->mem_words);
    return unknown_key(p, k);
}

static HvStatus begin_vm(Parser *p)
{
    if (p->cfg->n_vms >= CFG_MAX_VMS)
        return fail(p, "VMs demais no arquivo");
    VmConfig *vm = &p->cfg->vms[p->cfg->n_vms++];
    memset(vm, 0, sizeof *vm);
    vm->priority = 5;
    vm->mem_words = 512;
    vm->line = p->line;
    return HV_OK;
}

static HvStatus parse_section(Parser *p, char *s)
{
    static const char *const NAMES[] = {"host", "sched", "mem", "io", "log", "ext", "vm"};
    char *end = strchr(s, ']');
    if (!end || trim(end + 1)[0] != '\0')
        return fail(p, "cabecalho de secao mal formado");
    *end = '\0';
    char *name = trim(s + 1);
    for (int i = 0; i < 7; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            p->sec = (Section)(i + 1);
            return p->sec == SEC_VM ? begin_vm(p) : HV_OK;
        }
    }
    char msg[MSG_LEN];
    snprintf(msg, sizeof msg, "secao desconhecida '[%s]'", name);
    return fail(p, msg);
}

static HvStatus parse_line(Parser *p, char *line)
{
    char *c = strpbrk(line, ";#"); /* RF-CFG-03: comentario ate o fim da linha */
    if (c)
        *c = '\0';
    char *s = trim(line);
    if (*s == '\0')
        return HV_OK;
    if (*s == '[')
        return parse_section(p, s);
    char *eq = strchr(s, '=');
    if (!eq)
        return fail(p, "esperado 'chave = valor'");
    *eq = '\0';
    char *key = trim(s);
    char *val = trim(eq + 1);
    switch (p->sec) {
    case SEC_HOST: return key_host(p, key, val);
    case SEC_SCHED: return key_sched(p, key, val);
    case SEC_MEM: return key_mem(p, key, val);
    case SEC_IO: return key_io(p, key, val);
    case SEC_LOG: return key_log(p, key, val);
    case SEC_EXT: return key_ext(p, key, val);
    case SEC_VM: return key_vm(p, key, val);
    case SEC_NONE: break;
    }
    return fail(p, "chave fora de qualquer secao");
}

/* checagens que dependem de mais de uma chave */
static HvStatus validate(Parser *p)
{
    HvConfig *c = p->cfg;
    p->line = 0;
    if (c->disk_head >= c->disk_cylinders)
        return fail(p, "disk_head deve ser menor que disk_cylinders");
    for (int i = 0; i < c->n_vms; i++) {
        p->line = c->vms[i].line;
        if (c->vms[i].name[0] == '\0')
            return fail(p, "[vm] sem name");
        if (c->vms[i].program[0] == '\0')
            return fail(p, "[vm] sem program");
        for (int j = 0; j < i; j++)
            if (strcmp(c->vms[i].name, c->vms[j].name) == 0)
                return fail(p, "nome de VM repetido");
    }
    if (!p->log_file_set)
        strcpy(c->log_file, log_default_file(c->log_format));
    return HV_OK;
}

HvStatus config_parse_text(HvConfig *cfg, const char *name, const char *text, char *err,
                           size_t errlen)
{
    Parser p = {cfg, name, 0, SEC_NONE, false, err, errlen};
    char line[LINE_MAX_LEN];
    snprintf(cfg->path, sizeof cfg->path, "%s", name);
    while (*text) {
        size_t len = strcspn(text, "\n");
        p.line++;
        if (len >= sizeof line)
            return fail(&p, "linha longa demais");
        memcpy(line, text, len);
        line[len] = '\0';
        HvStatus st = parse_line(&p, line);
        if (st != HV_OK)
            return st;
        text += len;
        if (*text == '\n')
            text++;
    }
    return validate(&p);
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    while (buf) {
        len += fread(buf + len, 1, cap - len - 1, fp);
        if (len < cap - 1)
            break;
        char *bigger = realloc(buf, cap * 2);
        if (!bigger) {
            free(buf);
            buf = NULL;
            break;
        }
        buf = bigger;
        cap *= 2;
    }
    fclose(fp);
    if (buf)
        buf[len] = '\0';
    return buf;
}

HvStatus config_load(HvConfig *cfg, const char *path, char *err, size_t errlen)
{
    char *text = read_file(path);
    if (!text) {
        snprintf(err, errlen, "%s: nao foi possivel ler o arquivo", path);
        return HV_ERR_IO;
    }
    HvStatus st = config_parse_text(cfg, path, text, err, errlen);
    free(text);
    return st;
}
