#include "cpu/loader.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define LOADER_LINE_LEN 256
#define MAX_TOKENS 4
#define PATH_LEN 256

struct Program {
    char path[PATH_LEN];
    Instr *code;
    int len;
    int cap;
};

typedef enum ArgKind { A_REG, A_IMM, A_COUNT, A_TARGET, A_MEM } ArgKind;

typedef struct OpDef {
    const char *name;
    Opcode op;
    int nargs;
    ArgKind kind[2];
} OpDef;

static const OpDef OPS[] = {
    {"MOV", OP_MOV, 2, {A_REG, A_IMM}},     {"ADD", OP_ADD, 2, {A_REG, A_REG}},
    {"ADDI", OP_ADDI, 2, {A_REG, A_IMM}},   {"SUB", OP_SUB, 2, {A_REG, A_REG}},
    {"LOAD", OP_LOAD, 2, {A_REG, A_MEM}},   {"STORE", OP_STORE, 2, {A_REG, A_MEM}},
    {"JMP", OP_JMP, 1, {A_TARGET, A_REG}},  {"JZ", OP_JZ, 2, {A_REG, A_TARGET}},
    {"JNZ", OP_JNZ, 2, {A_REG, A_TARGET}},  {"OUT", OP_OUT, 1, {A_REG, A_REG}},
    {"DISK", OP_DISK, 1, {A_REG, A_REG}},   {"NET", OP_NET, 1, {A_COUNT, A_REG}},
    {"HALT", OP_HALT, 0, {A_REG, A_REG}},
};

#define N_OPS ((int)(sizeof OPS / sizeof OPS[0]))

typedef struct LineCtx {
    const char *name;
    int line;
    char *err;
    size_t errlen;
} LineCtx;

static HvStatus fail(const LineCtx *c, const char *msg, const char *tok)
{
    if (tok)
        snprintf(c->err, c->errlen, "%s:%d: %s '%s'", c->name, c->line, msg, tok);
    else
        snprintf(c->err, c->errlen, "%s:%d: %s", c->name, c->line, msg);
    return HV_ERR_PARSE;
}

static bool parse_reg(const char *t, int32_t *r)
{
    if (toupper((unsigned char)t[0]) != 'R' || t[1] < '0' || t[1] >= '0' + MV_NREGS ||
        t[2] != '\0')
        return false;
    *r = t[1] - '0';
    return true;
}

static bool parse_num(const char *t, int32_t *v)
{
    char *end;
    long x = strtol(t, &end, 10);
    if (*t == '\0' || *end != '\0' || x < INT32_MIN || x > INT32_MAX)
        return false;
    *v = (int32_t)x;
    return true;
}

/* le um operando conforme o tipo esperado; A_MEM aceita "addr" ou "[Rx]" */
static HvStatus parse_arg(const LineCtx *c, ArgKind k, const char *t, int32_t *v, bool *ind)
{
    size_t n = strlen(t);
    switch (k) {
    case A_REG:
        return parse_reg(t, v) ? HV_OK : fail(c, "registrador invalido", t);
    case A_IMM:
        return parse_num(t, v) ? HV_OK : fail(c, "numero invalido", t);
    case A_COUNT:
    case A_TARGET:
        if (!parse_num(t, v) || *v < 0)
            return fail(c, "esperava numero nao negativo", t);
        return HV_OK;
    case A_MEM:
        if (t[0] == '[' && n == 4 && t[3] == ']') {
            char reg[3] = {t[1], t[2], '\0'};
            *ind = true;
            return parse_reg(reg, v) ? HV_OK : fail(c, "registrador invalido", t);
        }
        if (!parse_num(t, v) || *v < 0)
            return fail(c, "endereco invalido", t);
        return HV_OK;
    }
    return fail(c, "operando invalido", t);
}

static int split(char *s, char *tok[MAX_TOKENS + 1])
{
    int n = 0;
    for (char *p = s; *p; p++)
        if (*p == ',')
            *p = ' ';
    char *p = s;
    while (*p && n <= MAX_TOKENS) {
        while (isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        tok[n++] = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p)
            *p++ = '\0';
    }
    return n;
}

static const OpDef *find_op(char *name)
{
    for (char *p = name; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    for (int i = 0; i < N_OPS; i++)
        if (strcmp(OPS[i].name, name) == 0)
            return &OPS[i];
    return NULL;
}

static HvStatus parse_instr(const LineCtx *c, char *s, Instr *out)
{
    char *tok[MAX_TOKENS + 1];
    int n = split(s, tok);
    const OpDef *def = find_op(tok[0]);
    if (!def)
        return fail(c, "instrucao desconhecida", tok[0]);
    if (n - 1 != def->nargs)
        return fail(c, "numero errado de operandos para", def->name);
    int32_t v[2] = {0, 0};
    bool ind = false;
    for (int i = 0; i < def->nargs; i++) {
        HvStatus st = parse_arg(c, def->kind[i], tok[i + 1], &v[i], &ind);
        if (st != HV_OK)
            return st;
    }
    out->op = def->op;
    if (ind) /* forma com registrador: LOADR / STORER */
        out->op = def->op == OP_LOAD ? OP_LOADR : OP_STORER;
    out->a = v[0];
    out->b = v[1];
    return HV_OK;
}

static bool program_push(Program *p, Instr in)
{
    if (p->len == p->cap) {
        int cap = p->cap ? p->cap * 2 : 16;
        Instr *novo = realloc(p->code, (size_t)cap * sizeof *novo);
        if (!novo)
            return false;
        p->code = novo;
        p->cap = cap;
    }
    p->code[p->len++] = in;
    return true;
}

static HvStatus parse_line(Program *prog, const LineCtx *c, char *line)
{
    char *semi = strchr(line, ';');
    if (semi)
        *semi = '\0';
    char *s = line;
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return HV_OK;
    Instr in = {OP_HALT, 0, 0};
    HvStatus st = parse_instr(c, s, &in);
    if (st != HV_OK)
        return st;
    return program_push(prog, in) ? HV_OK : HV_ERR_NOMEM;
}

static HvStatus parse_all(Program *prog, LineCtx *c, const char *text)
{
    char line[LOADER_LINE_LEN];
    while (*text) {
        size_t len = strcspn(text, "\n");
        c->line++;
        if (len >= sizeof line)
            return fail(c, "linha longa demais", NULL);
        memcpy(line, text, len);
        line[len] = '\0';
        HvStatus st = parse_line(prog, c, line);
        if (st != HV_OK)
            return st;
        text += len;
        if (*text == '\n')
            text++;
    }
    if (prog->len == 0)
        return fail(c, "programa vazio", NULL);
    return HV_OK;
}

HvStatus loader_parse_text(const char *name, const char *text, Program **out, char *err,
                           size_t errlen)
{
    Program *prog = calloc(1, sizeof *prog);
    if (!prog)
        return HV_ERR_NOMEM;
    snprintf(prog->path, sizeof prog->path, "%s", name);
    LineCtx c = {name, 0, err, errlen};
    HvStatus st = parse_all(prog, &c, text);
    if (st != HV_OK) {
        program_destroy(prog);
        return st;
    }
    *out = prog;
    return HV_OK;
}

HvStatus loader_load(const char *path, Program **out, char *err, size_t errlen)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err, errlen, "%s: nao foi possivel abrir o programa", path);
        return HV_ERR_IO;
    }
    size_t cap = 1024, len = 0;
    char *buf = malloc(cap);
    while (buf && (len += fread(buf + len, 1, cap - len - 1, fp)) == cap - 1) {
        char *bigger = realloc(buf, cap * 2);
        if (!bigger)
            free(buf);
        buf = bigger;
        cap *= 2;
    }
    fclose(fp);
    if (!buf)
        return HV_ERR_NOMEM;
    buf[len] = '\0';
    HvStatus st = loader_parse_text(path, buf, out, err, errlen);
    free(buf);
    return st;
}

void program_destroy(Program *prog)
{
    if (!prog)
        return;
    free(prog->code);
    free(prog);
}

int program_len(const Program *prog)
{
    return prog->len;
}

const Instr *program_at(const Program *prog, int i)
{
    return (i >= 0 && i < prog->len) ? &prog->code[i] : NULL;
}

const char *program_path(const Program *prog)
{
    return prog->path;
}
