#include "hv/hv.h"

#include <stdlib.h>
#include <string.h>

#include "hv/hv_internal.h"

#define DEFAULT_CONFIG "conf/hv.conf"
#define ERR_LEN 256
#define DESC_LEN 48

/* ---------- Observer ---------- */

static void hv_emit(void *ctx, const Event *ev)
{
    Hv *hv = ctx;
    for (int i = 0; i < hv->n_subs; i++)
        hv->subs[i].fn(hv->subs[i].ctx, ev);
}

HvStatus hv_subscribe(Hv *hv, EventFn fn, void *ctx)
{
    if (hv->n_subs >= MAX_SUBS)
        return HV_ERR_NOSPACE;
    hv->subs[hv->n_subs++] = (Subscriber){fn, ctx};
    return HV_OK;
}

static void log_subscriber(void *ctx, const Event *ev)
{
    if (event_is_internal(ev->type))
        return;
    log_write(ctx, ev->tick, ev->level, ev->module, ev->vm, event_name(ev->type), ev->detail);
}

static void emit_vm(Hv *hv, EventType t, const char *mod, const Vcb *vm, int cpu,
                    const char *det)
{
    Event ev = event_make(t, mod, hv->clock);
    ev.vm_id = vm_id(vm);
    ev.vm = vm_name(vm);
    ev.cpu = cpu;
    snprintf(ev.detail, sizeof ev.detail, "%s", det ? det : "");
    hv_emit(hv, &ev);
}

/* ---------- inicializacao (RF-HV-01) ---------- */

static HvStatus init_config(Hv *hv, const HvOptions *opt)
{
    char err[ERR_LEN];
    config_defaults(&hv->cfg);
    const char *path = opt->config_path ? opt->config_path : DEFAULT_CONFIG;
    HvStatus st = config_load(&hv->cfg, path, err, sizeof err);
    if (st != HV_OK) {
        fprintf(stderr, "%s\n", err);
        return st;
    }
    if (opt->policy_override)
        snprintf(hv->cfg.policy, sizeof hv->cfg.policy, "%s", opt->policy_override);
    return HV_OK;
}

static void on_io_done(void *ctx, Vcb *vm, DevId dev, uint64_t tick)
{
    Hv *hv = ctx;
    (void)dev;
    if (vm_set_state(vm, VM_READY, tick) == HV_OK)
        hv->pending[hv->n_pending++] = vm;
}

static HvStatus init_mem_io(Hv *hv)
{
    const HvConfig *c = &hv->cfg;
    MemConfig mc = {c->phys_words, c->allocation, &hv->sink};
    hv->mem = mem_create(&mc);
    if (!hv->mem) {
        fprintf(stderr, "erro: nao foi possivel criar a memoria (%s)\n", c->allocation);
        return HV_ERR_INVALID;
    }
    IoConfig ic = {c->disk_algorithm, c->disk_cylinders, c->disk_head, c->seek_rate,
                   hv->out,           &hv->sink,         on_io_done,   hv};
    hv->io = io_create(&ic);
    return hv->io ? HV_OK : HV_ERR_NOMEM;
}

static HvStatus init_sched_cpu(Hv *hv)
{
    const HvConfig *c = &hv->cfg;
    SchedConfig sc = {c->quantum, c->pcpus, c->seed, &hv->sink};
    hv->sched = sched_create(c->policy, &sc);
    if (!hv->sched) {
        fprintf(stderr, "erro: politica desconhecida '%s' (use CREDIT ou FCFS)\n", c->policy);
        return HV_ERR_INVALID;
    }
    hv->cpu = cpu_create(c->pcpus, &hv->sink);
    hv->preempt = calloc((size_t)c->pcpus, sizeof *hv->preempt);
    return (hv->cpu && hv->preempt) ? HV_OK : HV_ERR_NOMEM;
}

static void reject_vm(Hv *hv, const VmConfig *vc, const char *why)
{
    fprintf(stderr, "VM '%s' rejeitada: %s\n", vc->name, why);
    Event ev = event_make(EV_VM_REJECT, "vm", hv->clock);
    ev.level = LOG_ERROR;
    ev.vm = vc->name;
    snprintf(ev.detail, sizeof ev.detail, "%s", why);
    hv_emit(hv, &ev);
}

/* RF-VM-04: programa com erro rejeita so aquela VM, as outras seguem */
static HvStatus create_vm(Hv *hv, const VmConfig *vc)
{
    char err[ERR_LEN];
    Program *prog = NULL;
    if (vc->mem_words > hv->cfg.phys_words) {
        reject_vm(hv, vc, "mem_words maior que phys_words");
        return HV_OK;
    }
    HvStatus st = loader_load(vc->program, &prog, err, sizeof err);
    if (st == HV_ERR_NOMEM)
        return st;
    if (st != HV_OK) {
        reject_vm(hv, vc, err);
        return HV_OK;
    }
    if (program_len(prog) > vc->mem_words) {
        program_destroy(prog);
        reject_vm(hv, vc, "o programa nao cabe em mem_words");
        return HV_OK;
    }
    VmParams p = {hv->n_vms, vc->name, vc->priority, vc->mem_words, (uint64_t)vc->arrival};
    Vcb *vm = vm_create(&p, prog, &hv->sink, hv->clock);
    if (!vm) {
        program_destroy(prog);
        return HV_ERR_NOMEM;
    }
    hv->vms[hv->n_vms++] = vm;
    return HV_OK;
}

/* ordena por (chegada, id) com insercao simples; sao poucas VMs */
static void build_order(Hv *hv)
{
    for (int i = 0; i < hv->n_vms; i++) {
        Vcb *vm = hv->vms[i];
        int j = i;
        while (j > 0 && vm_arrival(hv->order[j - 1]) > vm_arrival(vm)) {
            hv->order[j] = hv->order[j - 1];
            j--;
        }
        hv->order[j] = vm;
    }
}

static HvStatus init_vms(Hv *hv)
{
    size_t n = hv->cfg.n_vms > 0 ? (size_t)hv->cfg.n_vms : 1;
    hv->vms = calloc(n, sizeof *hv->vms);
    hv->order = calloc(n, sizeof *hv->order);
    hv->pending = calloc(n, sizeof *hv->pending);
    if (!hv->vms || !hv->order || !hv->pending)
        return HV_ERR_NOMEM;
    for (int i = 0; i < hv->cfg.n_vms; i++) {
        HvStatus st = create_vm(hv, &hv->cfg.vms[i]);
        if (st != HV_OK)
            return st;
    }
    build_order(hv);
    return HV_OK;
}

static HvStatus init_ext(Hv *hv)
{
    ExtConfig ec = {hv->cfg.ext_enabled, hv->cfg.pcpus, hv->cfg.ext_file};
    hv->ext = ext_create(&ec);
    if (!hv->ext)
        return HV_ERR_NOMEM;
    return ec.enabled ? hv_subscribe(hv, ext_on_event, hv->ext) : HV_OK;
}

static HvStatus init_all(Hv *hv, const HvOptions *opt)
{
    hv->log = log_create();
    if (!hv->log)
        return HV_ERR_NOMEM;
    HvStatus st = init_config(hv, opt);
    if (st == HV_OK)
        st = log_open(hv->log, hv->cfg.log_format, hv->cfg.log_level, hv->cfg.log_file);
    if (st == HV_OK)
        st = hv_subscribe(hv, log_subscriber, hv->log);
    if (st == HV_OK)
        st = init_mem_io(hv);
    if (st == HV_OK)
        st = init_sched_cpu(hv);
    if (st == HV_OK)
        st = init_vms(hv);
    if (st == HV_OK)
        st = init_ext(hv);
    return st;
}

HvStatus hv_create(const HvOptions *opt, Hv **out)
{
    Hv *hv = calloc(1, sizeof *hv);
    if (!hv)
        return HV_ERR_NOMEM;
    hv->out = opt->out ? opt->out : stdout;
    hv->sink = (EventSink){hv_emit, hv};
    HvStatus st = init_all(hv, opt);
    if (st != HV_OK) { /* libera o que ja foi criado */
        hv_destroy(hv);
        return st;
    }
    hv_check_end(hv); /* sem nenhuma VM valida ja termina */
    *out = hv;
    return HV_OK;
}

/* RF-HV-05 */
void hv_destroy(Hv *hv)
{
    if (!hv)
        return;
    ext_destroy(hv->ext);
    for (int i = 0; i < hv->n_vms; i++)
        vm_destroy(hv->vms[i]);
    free(hv->vms);
    free(hv->order);
    free(hv->pending);
    free(hv->preempt);
    cpu_destroy(hv->cpu);
    sched_destroy(hv->sched);
    io_destroy(hv->io);
    mem_destroy(hv->mem);
    log_destroy(hv->log);
    free(hv);
}

/* ---------- laco de ticks (RF-HV-02) ---------- */

int hv_find_cpu(const Hv *hv, const Vcb *vm)
{
    for (int i = 0; i < hv->cfg.pcpus; i++)
        if (cpu_current(hv->cpu, i) == vm)
            return i;
    return -1;
}

static void sort_by_id(Vcb **v, int n)
{
    for (int i = 1; i < n; i++) {
        Vcb *x = v[i];
        int j = i;
        while (j > 0 && vm_id(v[j - 1]) > vm_id(x)) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
}

/* RF-SCH-04: quem ficou pronto no mesmo tick entra na fila em ordem de id */
static void flush_pending(Hv *hv)
{
    sort_by_id(hv->pending, hv->n_pending);
    for (int i = 0; i < hv->n_pending; i++)
        sched_enqueue(hv->sched, hv->pending[i], hv->clock);
    hv->n_pending = 0;
}

static int count_admitted(const Hv *hv)
{
    int n = 0;
    for (int i = 0; i < hv->n_vms; i++)
        if (vm_is_admitted(hv->vms[i]))
            n++;
    return n;
}

/* passo 1 - RF-VM-03 e RF-MEM-06: admite em ordem de chegada; se a
 * primeira da fila nao couber, as outras esperam atras dela */
static void step_admit(Hv *hv)
{
    int admitted = count_admitted(hv);
    for (int i = 0; i < hv->n_vms; i++) {
        Vcb *vm = hv->order[i];
        if (vm_state(vm) != VM_NEW)
            continue;
        if (vm_arrival(vm) > hv->clock || admitted >= hv->cfg.max_vms)
            break;
        int moved = 0;
        if (mem_alloc(hv->mem, vm, hv->clock, &moved) != HV_OK)
            break;
        hv->stall += moved; /* compactacao: 1 tick por particao movida */
        vm_admit(vm, hv->clock);
        hv->pending[hv->n_pending++] = vm;
        admitted++;
    }
}

void hv_stop_vm(Hv *hv, Vcb *vm, ExitReason why)
{
    int c = hv_find_cpu(hv, vm);
    if (c >= 0) {
        cpu_switch_out(hv->cpu, c);
        hv->preempt[c] = false;
    }
    bool tinha_memoria = vm_is_admitted(vm);
    vm_terminate(vm, why, hv->clock);
    sched_on_exit(hv->sched, vm, hv->clock);
    io_cancel_vm(hv->io, vm);
    if (tinha_memoria)
        mem_release(hv->mem, vm, hv->clock);
}

static void preempt_pending(Hv *hv)
{
    for (int i = 0; i < hv->cfg.pcpus; i++) {
        if (!hv->preempt[i] || !cpu_current(hv->cpu, i))
            continue;
        hv->preempt[i] = false;
        Vcb *vm = cpu_switch_out(hv->cpu, i);
        vm_set_state(vm, VM_READY, hv->clock);
        char det[DESC_LEN];
        snprintf(det, sizeof det, "cpu=%d", i);
        emit_vm(hv, EV_PREEMPT, "sched", vm, i, det);
        hv->pending[hv->n_pending++] = vm;
    }
    flush_pending(hv); /* chegadas e desbloqueios ja entraram antes, no passo 2 */
}

static void dispatch(Hv *hv, int c, Vcb *vm)
{
    char desc[DESC_LEN], det[EVENT_DETAIL_LEN];
    vm_set_state(vm, VM_RUNNING, hv->clock);
    vm_on_dispatch(vm, hv->clock);
    sched_describe(hv->sched, vm, desc, sizeof desc);
    snprintf(det, sizeof det, "cpu=%d %s", c, desc);
    emit_vm(hv, EV_DISPATCH, "sched", vm, c, det);
    cpu_switch_in(hv->cpu, c, vm, hv->clock);
}

/* passo 3 */
static void step_schedule(Hv *hv)
{
    sched_on_clock(hv->sched, hv->clock);
    preempt_pending(hv);
    for (int i = 0; i < hv->cfg.pcpus; i++) {
        if (cpu_current(hv->cpu, i))
            continue;
        Vcb *vm = sched_pick_next(hv->sched, i, hv->clock);
        if (vm)
            dispatch(hv, i, vm);
    }
}

static void handle_result(Hv *hv, int c, Vcb *vm, CpuResult r, bool fim_quantum)
{
    switch (r) {
    case CPU_OK:
        hv->preempt[c] = fim_quantum;
        break;
    case CPU_BLOCKED: /* RF-SCH-06: libera a pCPU no mesmo tick */
        cpu_switch_out(hv->cpu, c);
        hv->preempt[c] = false;
        vm_set_state(vm, VM_BLOCKED, hv->clock);
        sched_on_block(hv->sched, vm, hv->clock);
        break;
    case CPU_HALT: hv_stop_vm(hv, vm, EXIT_HALT); break;
    case CPU_PROT_FAULT: hv_stop_vm(hv, vm, EXIT_PROTECTION_FAULT); break;
    case CPU_BAD_INSTR: hv_stop_vm(hv, vm, EXIT_INVALID_INSTRUCTION); break;
    }
}

/* passo 4: cada pCPU ocupada executa uma instrucao */
static void step_execute(Hv *hv)
{
    for (int i = 0; i < hv->cfg.pcpus; i++) {
        Vcb *vm = cpu_current(hv->cpu, i);
        if (!vm)
            continue;
        CpuResult r = cpu_step(hv->cpu, i, hv->mem, hv->io, hv->clock);
        vm_count_cpu_tick(vm);
        emit_vm(hv, EV_EXEC, "cpu", vm, i, NULL);
        bool fim_quantum = sched_on_tick(hv->sched, vm, hv->clock);
        handle_result(hv, i, vm, r, fim_quantum);
    }
}

/* passo 5 */
static void step_stats(Hv *hv)
{
    for (int i = 0; i < hv->n_vms; i++)
        if (vm_state(hv->vms[i]) == VM_READY)
            vm_count_ready_tick(hv->vms[i]);
    mem_sample(hv->mem);
    Event ev = event_make(EV_TICK_END, "hv", hv->clock);
    hv_emit(hv, &ev);
}

void hv_check_end(Hv *hv)
{
    bool todas = true;
    for (int i = 0; i < hv->n_vms; i++)
        if (vm_state(hv->vms[i]) != VM_TERMINATED)
            todas = false;
    if (todas) {
        hv->finished = true;
        return;
    }
    if (hv->clock < (uint64_t)hv->cfg.max_ticks)
        return;
    for (int i = 0; i < hv->n_vms; i++)
        if (vm_state(hv->vms[i]) != VM_TERMINATED)
            hv_stop_vm(hv, hv->vms[i], EXIT_MAX_TICKS);
    hv->finished = true;
}

bool hv_step(Hv *hv)
{
    if (hv->finished)
        return false;
    if (hv->stall == 0)
        step_admit(hv);
    io_tick(hv->io, hv->clock);
    flush_pending(hv);
    if (hv->stall == 0) { /* durante a compactacao nenhuma VM executa */
        step_schedule(hv);
        step_execute(hv);
    }
    step_stats(hv);
    if (hv->stall > 0)
        hv->stall--;
    hv->clock++;
    hv_check_end(hv);
    return !hv->finished;
}

void hv_run(Hv *hv, uint64_t n)
{
    for (uint64_t k = 0; !hv->finished && (n == 0 || k < n); k++)
        hv_step(hv);
}

bool hv_finished(const Hv *hv) { return hv->finished; }
uint64_t hv_clock(const Hv *hv) { return hv->clock; }
