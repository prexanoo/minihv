#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mem/mem.h"

static int n_compact;

static void on_event(void *ctx, const Event *ev)
{
    (void)ctx;
    if (ev->type == EV_COMPACT)
        n_compact++;
}

static EventSink sink = {on_event, NULL};

static Vcb *nova_vm(int id, int words)
{
    char err[128];
    Program *p = NULL;
    assert(loader_parse_text("t.mv", "MOV R0, 1\nHALT\n", &p, err, sizeof err) == HV_OK);
    VmParams vp = {id, "vm", 5, words, 0};
    return vm_create(&vp, p, NULL, 0);
}

static Mem *nova_mem(int words)
{
    MemConfig mc = {words, "BEST_FIT", &sink};
    return mem_create(&mc);
}

static void alloc_ok(Mem *m, Vcb *vm, int base_esperada)
{
    int moved = -1;
    assert(mem_alloc(m, vm, 0, &moved) == HV_OK);
    assert(moved == 0);
    assert(vm_base(vm) == base_esperada);
}

/* buracos de 20 (em 0) e 10 (em 60): BEST_FIT pega o de 10, FIRST_FIT pegaria o 0 */
static void test_best_fit(void)
{
    Mem *m = nova_mem(100);
    Vcb *v[7];
    int sizes[5] = {20, 10, 30, 10, 30};
    int base = 0;
    for (int i = 0; i < 5; i++) {
        v[i] = nova_vm(i, sizes[i]);
        alloc_ok(m, v[i], base);
        base += sizes[i];
    }
    assert(mem_free_words(m) == 0);
    mem_release(m, v[0], 1);
    mem_release(m, v[3], 1);
    v[5] = nova_vm(5, 10);
    alloc_ok(m, v[5], 60);
    v[6] = nova_vm(6, 15);
    alloc_ok(m, v[6], 0);
    assert(mem_free_words(m) == 5);
    mem_destroy(m);
    for (int i = 0; i < 7; i++)
        vm_destroy(v[i]);
}

static void test_protecao(void)
{
    Mem *m = nova_mem(64);
    Vcb *a = nova_vm(0, 16), *b = nova_vm(1, 16);
    alloc_ok(m, a, 0);
    alloc_ok(m, b, 16);
    int32_t x;
    assert(mem_write(m, b, 15, 99) == HV_OK);
    assert(mem_read(m, b, 15, &x) == HV_OK && x == 99);
    assert(mem_read(m, a, 16, &x) == HV_ERR_PROTECTION); /* seria a memoria de b */
    assert(mem_read(m, a, -1, &x) == HV_ERR_PROTECTION);
    assert(mem_write(m, b, 16, 1) == HV_ERR_PROTECTION);
    int pa;
    assert(mem_translate(m, b, 3, &pa) == HV_OK && pa == 19);
    /* o codigo fica no inicio: palavra 0 tem o opcode de MOV */
    assert(mem_read(m, a, 0, &x) == HV_OK && x == OP_MOV);
    mem_destroy(m);
    vm_destroy(a);
    vm_destroy(b);
}

/* a, c saem -> buracos 30@0 e 30@60 + 10@90 (que se fundem em 40@60).
 * d (50) cabe no livre total (70) mas nao num buraco -> compacta (move b) */
static void test_coalescencia_e_compactacao(void)
{
    Mem *m = nova_mem(100);
    Vcb *a = nova_vm(0, 30), *b = nova_vm(1, 30), *c = nova_vm(2, 30), *d = nova_vm(3, 50);
    alloc_ok(m, a, 0);
    alloc_ok(m, b, 30);
    alloc_ok(m, c, 60);
    assert(mem_write(m, b, 20, 1234) == HV_OK);
    mem_release(m, a, 5);
    mem_release(m, c, 5);
    assert(mem_free_words(m) == 70);
    assert(mem_largest_hole(m) == 40);
    mem_sample(m); /* fragmentacao = 1 - 40/70 */

    int moved = 0;
    n_compact = 0;
    assert(mem_alloc(m, d, 6, &moved) == HV_OK);
    assert(moved == 1 && n_compact == 1 && mem_compactions(m) == 1);
    assert(vm_base(b) == 0 && vm_base(d) == 30);
    int32_t x;
    assert(mem_read(m, b, 20, &x) == HV_OK && x == 1234); /* dados foram junto */
    assert(mem_largest_hole(m) == 20);

    char linha[256];
    mem_report_line(m, linha, sizeof linha);
    assert(strstr(linha, "fragmentação_externa_média=42,9%") != NULL);
    assert(strstr(linha, "compactações=1") != NULL);

    Vcb *e = nova_vm(4, 30);
    assert(mem_alloc(m, e, 7, &moved) == HV_ERR_NOSPACE); /* so tem 20 livres */
    mem_destroy(m);
    vm_destroy(a);
    vm_destroy(b);
    vm_destroy(c);
    vm_destroy(d);
    vm_destroy(e);
}

int main(void)
{
    test_best_fit();
    test_protecao();
    test_coalescencia_e_compactacao();
    puts("test_mem: ok");
    return 0;
}
