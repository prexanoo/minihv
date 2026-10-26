#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vm/vm.h"

static int n_state_changes;
static char ultimo[EVENT_DETAIL_LEN];

static void on_event(void *ctx, const Event *ev)
{
    (void)ctx;
    if (ev->type == EV_STATE_CHANGE) {
        n_state_changes++;
        snprintf(ultimo, sizeof ultimo, "%s", ev->detail);
    }
}

static Vcb *nova_vm(const EventSink *sink)
{
    char err[128];
    Program *p = NULL;
    assert(loader_parse_text("t.mv", "HALT\n", &p, err, sizeof err) == HV_OK);
    VmParams vp = {0, "web", 2, 64, 3};
    return vm_create(&vp, p, sink, 0);
}

static void test_transicoes(void)
{
    EventSink sink = {on_event, NULL};
    Vcb *vm = nova_vm(&sink);
    assert(vm_state(vm) == VM_NEW);
    assert(!vm_is_admitted(vm));

    /* invalidas a partir de NEW */
    assert(vm_set_state(vm, VM_RUNNING, 0) == HV_ERR_INVALID);
    assert(vm_set_state(vm, VM_BLOCKED, 0) == HV_ERR_INVALID);
    assert(n_state_changes == 0); /* transicao rejeitada nao vai pro log */

    assert(vm_admit(vm, 3) == HV_OK);
    assert(strcmp(ultimo, "NEW->READY") == 0);
    assert(vm_is_admitted(vm));
    assert(vm_set_state(vm, VM_BLOCKED, 4) == HV_ERR_INVALID); /* READY nao bloqueia */
    assert(vm_set_state(vm, VM_RUNNING, 4) == HV_OK);
    assert(vm_set_state(vm, VM_BLOCKED, 5) == HV_OK);
    assert(vm_set_state(vm, VM_PAUSED, 5) == HV_ERR_INVALID);   /* pause em BLOCKED */
    assert(vm_set_state(vm, VM_RUNNING, 6) == HV_ERR_INVALID);  /* tem que passar por READY */
    assert(vm_set_state(vm, VM_READY, 6) == HV_OK);
    assert(vm_set_state(vm, VM_PAUSED, 6) == HV_OK);
    assert(vm_set_state(vm, VM_READY, 7) == HV_OK);
    assert(vm_set_state(vm, VM_RUNNING, 7) == HV_OK);
    assert(vm_terminate(vm, EXIT_HALT, 9) == HV_OK);
    assert(strcmp(ultimo, "RUNNING->TERMINATED") == 0);
    assert(vm_exit_reason(vm) == EXIT_HALT && vm_end_tick(vm) == 9);

    /* TERMINATED e final */
    for (int s = 0; s < VM_N_STATES; s++)
        assert(vm_set_state(vm, (VmState)s, 10) == HV_ERR_INVALID);
    assert(n_state_changes == 8);
    vm_destroy(vm);
}

static void test_contexto_e_contadores(void)
{
    Vcb *vm = nova_vm(NULL);
    int32_t regs[MV_NREGS] = {1, -2, 3, 4}, r2[MV_NREGS], pc;
    vm_set_context(vm, regs, 7);
    vm_get_context(vm, r2, &pc);
    assert(memcmp(regs, r2, sizeof regs) == 0 && pc == 7);

    assert(!vm_was_dispatched(vm));
    vm_on_dispatch(vm, 10);
    vm_on_dispatch(vm, 20); /* so o primeiro conta para resposta */
    assert(vm_first_dispatch(vm) == 10);
    vm_count_ready_tick(vm);
    vm_count_ready_tick(vm);
    vm_count_cpu_tick(vm);
    assert(vm_wait_ticks(vm) == 2 && vm_cpu_ticks(vm) == 1);
    assert(strcmp(vm_exit_name(EXIT_PROTECTION_FAULT), "PROTECTION_FAULT") == 0);
    vm_destroy(vm);
}

int main(void)
{
    test_transicoes();
    test_contexto_e_contadores();
    puts("test_vm: ok");
    return 0;
}
