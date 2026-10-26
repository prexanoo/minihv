#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "io/io.h"

#define MAX_DONE 16

typedef struct Done {
    int n;
    int vm[MAX_DONE];
    DevId dev[MAX_DONE];
    uint64_t tick[MAX_DONE];
} Done;

static void on_done(void *ctx, Vcb *vm, DevId dev, uint64_t tick)
{
    Done *d = ctx;
    d->vm[d->n] = vm_id(vm);
    d->dev[d->n] = dev;
    d->tick[d->n] = tick;
    d->n++;
}

static Vcb *nova_vm(int id, const char *nome)
{
    char err[128];
    Program *p = NULL;
    assert(loader_parse_text("t.mv", "HALT\n", &p, err, sizeof err) == HV_OK);
    VmParams vp = {id, nome, 5, 16, 0};
    return vm_create(&vp, p, NULL, 0);
}

static Io *novo_io(Done *d, FILE *console)
{
    IoConfig ic = {"C_LOOK", 200, 50, 25, console, NULL, on_done, d};
    return io_create(&ic);
}

/* Fila no tick 0: 98, 183, 37, 122. Cabeca em 50 subindo:
 *  t1: 98  (dist 48  -> 1 + 2 = 3 ticks) termina t4
 *  t4: 122 (dist 24  -> 1 + 1 = 2)       termina t6
 *  t6: 183 (dist 61  -> 1 + 3 = 4)       termina t10
 *  t10: nada acima, salta para 37 (dist 146 -> 1 + 6 = 7) termina t17
 * deslocamento total = 48 + 24 + 61 + 146 = 279 */
static void test_c_look(void)
{
    Done d = {0};
    FILE *con = tmpfile();
    Io *io = novo_io(&d, con);
    int cil[4] = {98, 183, 37, 122};
    Vcb *v[4];
    for (int i = 0; i < 4; i++) {
        v[i] = nova_vm(i, "d");
        assert(io_request(io, v[i], DEV_DISK, cil[i], 0) == HV_OK);
    }
    for (uint64_t t = 1; t <= 20; t++)
        io_tick(io, t);
    assert(d.n == 4);
    int ordem[4] = {0, 3, 1, 2};
    uint64_t fim[4] = {4, 6, 10, 17};
    for (int i = 0; i < 4; i++) {
        assert(d.vm[i] == ordem[i]);
        assert(d.tick[i] == fim[i]);
    }
    char linhas[512];
    io_report_lines(io, linhas, sizeof linhas);
    assert(strstr(linhas, "deslocamento_total=279 cilindros") != NULL);
    io_destroy(io);
    fclose(con);
    for (int i = 0; i < 4; i++)
        vm_destroy(v[i]);
}

/* CONSOLE = 2 ticks, NET 16 palavras = 2 + 2 = 4 ticks, ambos comecam em t1.
 * Cilindro negativo vira positivo pelo mod. */
static void test_latencias_e_console(void)
{
    Done d = {0};
    FILE *con = tmpfile();
    Io *io = novo_io(&d, con);
    Vcb *a = nova_vm(0, "web"), *b = nova_vm(1, "net"), *c = nova_vm(2, "dsk");
    io_request(io, a, DEV_CONSOLE, 42, 0);
    io_request(io, b, DEV_NET, 16, 0);
    io_request(io, c, DEV_DISK, -150, 0); /* -150 mod 200 = 50: distancia 0 */
    for (uint64_t t = 1; t <= 6; t++)
        io_tick(io, t);
    assert(d.n == 3);
    assert(d.dev[0] == DEV_DISK && d.tick[0] == 2); /* 1 + 0 */
    assert(d.dev[1] == DEV_CONSOLE && d.tick[1] == 3);
    assert(d.dev[2] == DEV_NET && d.tick[2] == 5);
    rewind(con);
    char linha[64];
    assert(fgets(linha, sizeof linha, con) && strcmp(linha, "[t=0003] web: 42\n") == 0);
    io_destroy(io);
    fclose(con);
    vm_destroy(a);
    vm_destroy(b);
    vm_destroy(c);
}

/* requisicao de VM morta nao gera interrupcao */
static void test_cancelamento(void)
{
    Done d = {0};
    Io *io = novo_io(&d, NULL);
    Vcb *a = nova_vm(0, "a"), *b = nova_vm(1, "b");
    io_request(io, a, DEV_NET, 8, 0);
    io_request(io, b, DEV_NET, 8, 0);
    io_tick(io, 1); /* a em atendimento, b na fila */
    io_cancel_vm(io, a);
    io_cancel_vm(io, b);
    for (uint64_t t = 2; t <= 10; t++)
        io_tick(io, t);
    assert(d.n == 0);
    io_destroy(io);
    vm_destroy(a);
    vm_destroy(b);
}

int main(void)
{
    test_c_look();
    test_latencias_e_console();
    test_cancelamento();
    puts("test_io: ok");
    return 0;
}
