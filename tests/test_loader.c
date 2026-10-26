#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cpu/loader.h"

static char err[256];

static void test_contador(void)
{
    Program *p = NULL;
    assert(loader_load("progs/contador.mv", &p, err, sizeof err) == HV_OK);
    assert(program_len(p) == 6);
    assert(program_at(p, 0)->op == OP_MOV && program_at(p, 0)->a == 0 &&
           program_at(p, 0)->b == 5);
    assert(program_at(p, 2)->op == OP_OUT);
    assert(program_at(p, 4)->op == OP_JNZ && program_at(p, 4)->b == 2);
    assert(program_at(p, 5)->op == OP_HALT);
    assert(program_at(p, 6) == NULL);
    program_destroy(p);
}

static void test_formas(void)
{
    const char *txt = "  mov r1, -7\n"
                      "\n"
                      "; so comentario\n"
                      "LOAD R0, 10\n"
                      "LOAD R0, [R1]\n"
                      "STORE R2, [R3]\n"
                      "STORE R2,20\n"
                      "NET 16\n"
                      "DISK R3\n"
                      "JMP 0\n"
                      "HALT ; fim\n";
    Program *p = NULL;
    assert(loader_parse_text("t.mv", txt, &p, err, sizeof err) == HV_OK);
    assert(program_len(p) == 9); /* linhas vazias e comentarios nao contam */
    assert(program_at(p, 0)->op == OP_MOV && program_at(p, 0)->b == -7);
    assert(program_at(p, 1)->op == OP_LOAD && program_at(p, 1)->b == 10);
    assert(program_at(p, 2)->op == OP_LOADR && program_at(p, 2)->b == 1);
    assert(program_at(p, 3)->op == OP_STORER && program_at(p, 3)->a == 2 &&
           program_at(p, 3)->b == 3);
    assert(program_at(p, 4)->op == OP_STORE && program_at(p, 4)->b == 20);
    assert(program_at(p, 5)->op == OP_NET && program_at(p, 5)->a == 16);
    program_destroy(p);
}

static void expect_error(const char *txt, const char *msg)
{
    Program *p = NULL;
    assert(loader_parse_text("t.mv", txt, &p, err, sizeof err) == HV_ERR_PARSE);
    assert(p == NULL);
    assert(strcmp(err, msg) == 0);
}

static void test_erros(void)
{
    expect_error("MOV R0, 1\nPULA 3\n", "t.mv:2: instrucao desconhecida 'PULA'");
    expect_error("MOV R4, 1\n", "t.mv:1: registrador invalido 'R4'");
    expect_error("ADD R0\n", "t.mv:1: numero errado de operandos para 'ADD'");
    expect_error("HALT\nHALT 1\n", "t.mv:2: numero errado de operandos para 'HALT'");
    expect_error("MOV R0, x\n", "t.mv:1: numero invalido 'x'");
    expect_error("LOAD R0, -1\n", "t.mv:1: endereco invalido '-1'");
    expect_error("JMP -2\n", "t.mv:1: esperava numero nao negativo '-2'");
    expect_error("; nada\n\n", "t.mv:2: programa vazio");

    Program *p = NULL;
    assert(loader_load("progs/erro_sintaxe.mv", &p, err, sizeof err) == HV_ERR_PARSE);
    assert(strcmp(err, "progs/erro_sintaxe.mv:3: registrador invalido 'R9'") == 0);
    assert(loader_load("progs/nao_existe.mv", &p, err, sizeof err) == HV_ERR_IO);
}

int main(void)
{
    test_contador();
    test_formas();
    test_erros();
    puts("test_loader: ok");
    return 0;
}
