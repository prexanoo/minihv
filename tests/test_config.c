#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config/config.h"

static HvConfig cfg;
static char err[CFG_ERR_LEN];

static HvStatus parse(const char *text)
{
    config_defaults(&cfg);
    err[0] = '\0';
    return config_parse_text(&cfg, "t.conf", text, err, sizeof err);
}

static void test_defaults_escopo(void)
{
    assert(parse("") == HV_OK);
    assert(cfg.pcpus == 4 && cfg.max_vms == 6);
    assert(strcmp(cfg.policy, "CREDIT") == 0 && cfg.quantum == 5);
    assert(strcmp(cfg.mem_model, "CONT") == 0 && cfg.phys_words == 4096);
    assert(strcmp(cfg.allocation, "BEST_FIT") == 0);
    assert(strcmp(cfg.disk_algorithm, "C_LOOK") == 0);
    assert(cfg.disk_cylinders == 200 && cfg.disk_head == 50 && cfg.seek_rate == 25);
    assert(cfg.log_format == LOG_FMT_JSONL);
    assert(strcmp(cfg.log_file, "out/minihv.jsonl") == 0);
    assert(cfg.seed == 2026 && cfg.ext_enabled);
}

static void test_valida(void)
{
    const char *txt = "; comentario\n"
                      "# outro comentario\n"
                      "[host]\n"
                      "pcpus = 2   ; comentario no fim\n"
                      "\n"
                      "[sched]\n"
                      "policy = FCFS\n"
                      "[log]\n"
                      "format = CSV\n"
                      "[vm]\n"
                      "name = web\n"
                      "program = progs/contador.mv\n"
                      "arrival = 3\n"
                      "[vm]\n"
                      "name = db\n"
                      "program = progs/disco.mv\n"
                      "priority = 9\r\n";
    assert(parse(txt) == HV_OK);
    assert(cfg.pcpus == 2);
    assert(strcmp(cfg.policy, "FCFS") == 0);
    assert(cfg.log_format == LOG_FMT_CSV);
    assert(strcmp(cfg.log_file, "out/minihv.csv") == 0); /* padrao segue o formato */
    assert(cfg.n_vms == 2);
    assert(strcmp(cfg.vms[0].name, "web") == 0 && cfg.vms[0].arrival == 3);
    assert(cfg.vms[0].priority == 5 && cfg.vms[0].mem_words == 512); /* padroes */
    assert(cfg.vms[1].priority == 9);
}

static void test_erros(void)
{
    assert(parse("[sched]\nquantum = 99\n") == HV_ERR_PARSE);
    assert(strcmp(err, "t.conf:2: quantum deve estar entre 1 e 50") == 0);

    assert(parse("[host]\n\npcpus = 2\nvelocidade = 3\n") == HV_ERR_PARSE);
    assert(strcmp(err, "t.conf:4: chave desconhecida 'velocidade'") == 0);

    assert(parse("[cpu]\n") == HV_ERR_PARSE);
    assert(strstr(err, "t.conf:1: secao desconhecida") == err);

    assert(parse("[host]\npcpus = dois\n") == HV_ERR_PARSE);
    assert(strstr(err, "t.conf:2:") == err);

    assert(parse("[sched]\npolicy = RR\n") == HV_ERR_PARSE);
    assert(strstr(err, "policy invalido") != NULL);

    assert(parse("pcpus = 2\n") == HV_ERR_PARSE); /* fora de secao */
    assert(parse("[host]\npcpus 2\n") == HV_ERR_PARSE); /* sem '=' */
    assert(parse("[mem]\nmodel = PAG\n") == HV_ERR_PARSE); /* nao e o escopo */
    assert(parse("[ext]\nenabled = talvez\n") == HV_ERR_PARSE);
}

static void test_erros_vm(void)
{
    assert(parse("[vm]\nname = a\n") == HV_ERR_PARSE);
    assert(strcmp(err, "t.conf:1: [vm] sem program") == 0);

    assert(parse("[vm]\nname=a\nprogram=x\n[vm]\nname=a\nprogram=y\n") == HV_ERR_PARSE);
    assert(strcmp(err, "t.conf:4: nome de VM repetido") == 0);

    assert(parse("[vm]\nname = a b\n") == HV_ERR_PARSE);
    assert(parse("[vm]\nname=a\nprogram=x\npriority=10\n") == HV_ERR_PARSE);
    assert(parse("[io]\ndisk_head = 300\n") == HV_ERR_PARSE);
}

static void test_arquivo_inexistente(void)
{
    config_defaults(&cfg);
    assert(config_load(&cfg, "nao/existe.conf", err, sizeof err) == HV_ERR_IO);
    assert(strstr(err, "nao/existe.conf") == err);
}

int main(void)
{
    test_defaults_escopo();
    test_valida();
    test_erros();
    test_erros_vm();
    test_arquivo_inexistente();
    puts("test_config: ok");
    return 0;
}
