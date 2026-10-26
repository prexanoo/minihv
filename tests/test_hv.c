/* testes de ponta a ponta com os cenarios de conf/ (TA-01, 04, 05, 06, 07) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv/hv.h"

#define MAX_EXITS 16

typedef struct Exits {
    int n;
    char vm[MAX_EXITS][16];
    char detail[MAX_EXITS][EVENT_DETAIL_LEN];
    int compact;
    char compact_detail[EVENT_DETAIL_LEN];
} Exits;

static void on_event(void *ctx, const Event *ev)
{
    Exits *ex = ctx;
    if (ev->type == EV_VM_EXIT && ex->n < MAX_EXITS) {
        snprintf(ex->vm[ex->n], sizeof ex->vm[0], "%s", ev->vm);
        snprintf(ex->detail[ex->n], sizeof ex->detail[0], "%s", ev->detail);
        ex->n++;
    }
    if (ev->type == EV_COMPACT) {
        ex->compact++;
        snprintf(ex->compact_detail, sizeof ex->compact_detail, "%s", ev->detail);
    }
}

/* le um arquivo inteiro (precisa ser liberado) */
static char *slurp(FILE *fp, long *len)
{
    fflush(fp);
    fseek(fp, 0, SEEK_END);
    *len = ftell(fp);
    rewind(fp);
    char *buf = malloc((size_t)*len + 1);
    assert(buf);
    assert(fread(buf, 1, (size_t)*len, fp) == (size_t)*len);
    buf[*len] = '\0';
    return buf;
}

/* roda um cenario em modo batch e devolve toda a saida padrao */
static char *run_batch(const char *conf, Exits *ex, long *len)
{
    FILE *out = tmpfile();
    HvOptions opt = {conf, NULL, out};
    Hv *hv = NULL;
    assert(hv_create(&opt, &hv) == HV_OK);
    if (ex) {
        memset(ex, 0, sizeof *ex);
        hv_subscribe(hv, on_event, ex);
    }
    hv_run(hv, 0);
    hv_finish(hv, out);
    hv_destroy(hv);
    char *txt = slurp(out, len);
    fclose(out);
    return txt;
}

static void test_ta01_contador(void)
{
    long len;
    char *out = run_batch("conf/ta01.conf", NULL, &len);
    /* OUT em t2, console comeca em t3 e termina em t5; depois SUB, JNZ e
     * o proximo OUT em t7 -> t10, e assim por diante (5 ticks por volta) */
    const char *linhas[] = {"[t=0005] web: 5\n", "[t=0010] web: 4\n", "[t=0015] web: 3\n",
                            "[t=0020] web: 2\n", "[t=0025] web: 1\n"};
    const char *p = out;
    for (int i = 0; i < 5; i++) { /* na ordem certa */
        p = strstr(p, linhas[i]);
        assert(p != NULL);
    }
    assert(strstr(out, "== MiniHV | escopo 09 | policy=CREDIT q=5 | mem=CONT/BEST_FIT | "
                       "disk=C_LOOK ==") != NULL);
    assert(strstr(out, "web   HALT") != NULL);
    free(out);
}

static void test_ta05_protecao(void)
{
    Exits ex;
    long len;
    free(run_batch("conf/ta05.conf", &ex, &len));
    assert(ex.n == 3);
    for (int i = 0; i < ex.n; i++) {
        if (strcmp(ex.vm[i], "bug") == 0)
            assert(strstr(ex.detail[i], "reason=PROTECTION_FAULT") == ex.detail[i]);
        else
            assert(strstr(ex.detail[i], "reason=HALT") == ex.detail[i]);
    }
}

static void test_ta04_memoria(void)
{
    Exits ex;
    long len;
    char *out = run_batch("conf/ta04.conf", &ex, &len);
    assert(ex.n == 6); /* todas terminam, nenhuma falha */
    for (int i = 0; i < ex.n; i++)
        assert(strstr(ex.detail[i], "reason=HALT") == ex.detail[i]);
    assert(ex.compact == 1);
    assert(strncmp(ex.compact_detail, "moved=2 ticks=2", 15) == 0);
    assert(strstr(out, "compactações=1") != NULL);
    free(out);
}

static void test_ta06_programa_invalido(void)
{
    Exits ex;
    long len;
    char *out = run_batch("conf/ta06_programa.conf", &ex, &len);
    assert(ex.n == 1 && strcmp(ex.vm[0], "web") == 0); /* so a VM valida rodou */
    free(out);

    HvOptions opt = {"conf/erros/chave_invalida.conf", NULL, NULL};
    Hv *hv = NULL;
    assert(hv_create(&opt, &hv) == HV_ERR_PARSE);
    assert(hv == NULL);
}

static char *read_file(const char *path, long *len)
{
    FILE *fp = fopen(path, "rb");
    assert(fp);
    char *txt = slurp(fp, len);
    fclose(fp);
    return txt;
}

/* TA-07: mesma configuracao duas vezes -> saida e log identicos byte a byte */
static void test_ta07_determinismo(void)
{
    long l1, l2, g1, g2;
    char *s1 = run_batch("conf/hv.conf", NULL, &l1);
    char *log1 = read_file("out/minihv.jsonl", &g1);
    char *s2 = run_batch("conf/hv.conf", NULL, &l2);
    char *log2 = read_file("out/minihv.jsonl", &g2);
    assert(l1 == l2 && memcmp(s1, s2, (size_t)l1) == 0);
    assert(g1 == g2 && memcmp(log1, log2, (size_t)g1) == 0);
    free(s1);
    free(s2);
    free(log1);
    free(log2);
}

int main(void)
{
    test_ta01_contador();
    test_ta05_protecao();
    test_ta04_memoria();
    test_ta06_programa_invalido();
    test_ta07_determinismo();
    puts("test_hv: ok");
    return 0;
}
