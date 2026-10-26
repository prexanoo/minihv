#ifndef CPU_LOADER_H
#define CPU_LOADER_H

#include <stdint.h>
#include <stdlib.h>

#include "util/status.h"

/* Leitor de programas MV-ISA (.mv). So depende de util, por isso pode
 * ser usado tanto pela cpu quanto pelo vm sem criar ciclo. */

typedef enum Opcode {
    OP_MOV,
    OP_ADD,
    OP_ADDI,
    OP_SUB,
    OP_LOAD,   /* LOAD Rd, addr */
    OP_LOADR,  /* LOAD Rd, [Rs] */
    OP_STORE,  /* STORE Rs, addr */
    OP_STORER, /* STORE Rs, [Rd] */
    OP_JMP,
    OP_JZ,
    OP_JNZ,
    OP_OUT,
    OP_DISK,
    OP_NET,
    OP_HALT
} Opcode;

#define MV_NREGS 4

/* a = registrador destino/fonte ou alvo do JMP; b = segundo operando */
typedef struct Instr {
    Opcode op;
    int32_t a;
    int32_t b;
} Instr;

typedef struct Program Program;

HvStatus loader_load(const char *path, Program **out, char *err, size_t errlen);
HvStatus loader_parse_text(const char *name, const char *text, Program **out, char *err,
                           size_t errlen);
void program_destroy(Program *prog);

int program_len(const Program *prog);
const Instr *program_at(const Program *prog, int i);
const char *program_path(const Program *prog);
const char *opcode_name(Opcode op);

#endif
