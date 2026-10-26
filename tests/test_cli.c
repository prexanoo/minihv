/* modo interativo alimentado por um arquivo de comandos */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli/cli.h"

static char *run_cli(const char *conf, const char *comandos)
{
    FILE *in = tmpfile(), *out = tmpfile();
    fputs(comandos, in);
    rewind(in);
    HvOptions opt = {conf, NULL, out};
    Hv *hv = NULL;
    assert(hv_create(&opt, &hv) == HV_OK);
    cli_run(hv, in, out);
    hv_destroy(hv);
    long len = ftell(out);
    rewind(out);
    char *buf = calloc((size_t)len + 1, 1);
    assert(buf && fread(buf, 1, (size_t)len, out) == (size_t)len);
    fclose(in);
    fclose(out);
    return buf;
}

static void test_comandos(void)
{
    char *s = run_cli("conf/ta05.conf", "step\n"
                                        "run 2\n"
                                        "ps\n"
                                        "voar\n"
                                        "pause calc\n"
                                        "pause calc\n"
                                        "resume calc\n"
                                        "kill calc\n"
                                        "kill calc\n"
                                        "pause nada\n"
                                        "mem\n"
                                        "io\n"
                                        "stats\n"
                                        "run\n"
                                        "step\n"
                                        "quit\n");
    assert(strstr(s, "minihv> ") != NULL);
    assert(strstr(s, "t=1\n") && strstr(s, "t=3\n"));
    assert(strstr(s, "comando invalido: 'voar'") != NULL);
    assert(strstr(s, "comandos:") != NULL);      /* ajuda depois do comando invalido */
    assert(strstr(s, "calc pausada") != NULL); /* estava RUNNING em t=3 */
    assert(strstr(s, "pause recusado: calc esta em PAUSED") != NULL);
    assert(strstr(s, "calc voltou para READY") != NULL);
    assert(strstr(s, "calc encerrada (KILLED)") != NULL);
    assert(strstr(s, "calc ja terminou") != NULL);
    assert(strstr(s, "VM 'nada' nao existe") != NULL);
    assert(strstr(s, "memoria CONT/BEST_FIT") != NULL);
    assert(strstr(s, "disco C_LOOK") != NULL);
    assert(strstr(s, "simulacao terminada") != NULL);
    assert(strstr(s, "a simulacao ja terminou") != NULL);
    assert(strstr(s, "KILLED") != NULL);
    free(s);
}

/* pause numa VM bloqueada e recusado; EOF sai como quit */
static void test_pause_bloqueada(void)
{
    /* em t=3 a VM web (contador) esta BLOCKED esperando o console */
    char *s = run_cli("conf/ta01.conf", "run 3\nps\npause web\n");
    assert(strstr(s, "BLOCKED") != NULL);
    assert(strstr(s, "pause recusado: web esta em BLOCKED") != NULL);
    free(s);
}

int main(void)
{
    test_comandos();
    test_pause_bloqueada();
    puts("test_cli: ok");
    return 0;
}
