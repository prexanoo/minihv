/* Relatorio final e comandos do modo interativo (parte do modulo hv). */
#include <stdlib.h>
#include <string.h>

#include "hv/hv_internal.h"
#include "log/stats.h"

#define LINE_BUF 256
#define IO_BUF 512
#define DESC_LEN 48

/* ---------- relatorio (RF-LOG-04) ---------- */

static void fill_row(const Vcb *vm, StatsVmRow *r)
{
    bool fim = vm_state(vm) == VM_TERMINATED;
    r->name = vm_name(vm);
    r->estado = fim ? vm_exit_name(vm_exit_reason(vm)) : vm_state_name(vm_state(vm));
    r->chegada = (long long)vm_arrival(vm);
    r->fim = fim ? (long long)vm_end_tick(vm) : STATS_NA;
    r->espera = (long long)vm_wait_ticks(vm);
    r->resposta = vm_was_dispatched(vm)
                      ? (long long)(vm_first_dispatch(vm) - vm_arrival(vm))
                      : STATS_NA;
    r->cpu = (long long)vm_cpu_ticks(vm);
    r->faults = STATS_NA; /* nao existe page fault no modelo CONT */
    r->io = (long long)vm_io_count(vm);
}

void hv_print_report(const Hv *hv, FILE *out)
{
    StatsVmRow *rows = calloc((size_t)hv->n_vms + 1, sizeof *rows);
    uint64_t *busy = calloc((size_t)hv->cfg.pcpus, sizeof *busy);
    if (!rows || !busy) {
        free(rows);
        free(busy);
        return;
    }
    for (int i = 0; i < hv->n_vms; i++)
        fill_row(hv->vms[i], &rows[i]);
    for (int i = 0; i < hv->cfg.pcpus; i++)
        busy[i] = cpu_busy_ticks(hv->cpu, i);
    char mem_desc[DESC_LEN], mem_line[LINE_BUF], io_lines[IO_BUF];
    snprintf(mem_desc, sizeof mem_desc, "CONT/%s", mem_policy_name(hv->mem));
    mem_report_line(hv->mem, mem_line, sizeof mem_line);
    io_report_lines(hv->io, io_lines, sizeof io_lines);
    StatsReport rep = {HV_ESCOPO, sched_name(hv->sched),
                       sched_uses_quantum(hv->sched) ? sched_quantum(hv->sched) : 0,
                       mem_desc, io_disk_algorithm(hv->io), hv->clock, hv->cfg.pcpus,
                       cpu_ctx_switches(hv->cpu), rows, hv->n_vms, busy, mem_line, io_lines};
    stats_print_report(out, &rep);
    free(rows);
    free(busy);
}

void hv_finish(Hv *hv, FILE *out)
{
    hv_print_report(hv, out);
    ext_finish(hv->ext, out);
}

/* ---------- consultas ---------- */

void hv_print_ps(const Hv *hv, FILE *out)
{
    fprintf(out, "%-3s %-10s %-11s %-4s %-5s %-4s %-6s %-6s %s\n", "id", "nome", "estado",
            "prio", "pc", "cpu", "base", "limite", "sched");
    for (int i = 0; i < hv->n_vms; i++) {
        const Vcb *vm = hv->vms[i];
        int32_t regs[MV_NREGS], pc;
        int c = hv_find_cpu(hv, vm);
        vm_get_context(vm, regs, &pc);
        if (c >= 0) /* rodando: o PC atual esta na pCPU, nao no VCB */
            pc = cpu_pc(hv->cpu, c);
        char desc[DESC_LEN], cpu_txt[8];
        sched_describe(hv->sched, vm, desc, sizeof desc);
        snprintf(cpu_txt, sizeof cpu_txt, c >= 0 ? "%d" : "-", c);
        fprintf(out, "%-3d %-10s %-11s %-4d %-5d %-4s %-6d %-6d %s\n", vm_id(vm), vm_name(vm),
                vm_state_name(vm_state(vm)), vm_priority(vm), (int)pc, cpu_txt, vm_base(vm),
                vm_limit(vm), desc);
    }
}

void hv_print_mem(const Hv *hv, FILE *out) { mem_print(hv->mem, out); }
void hv_print_io(const Hv *hv, FILE *out) { io_print(hv->io, out); }

/* ---------- pause / resume / kill ---------- */

/* aceita o nome ou o id da VM */
static Vcb *find_vm(const Hv *hv, const char *s, char *msg, size_t n)
{
    char *end;
    long id = strtol(s, &end, 10);
    for (int i = 0; i < hv->n_vms; i++) {
        if (strcmp(vm_name(hv->vms[i]), s) == 0)
            return hv->vms[i];
        if (*s && *end == '\0' && vm_id(hv->vms[i]) == id)
            return hv->vms[i];
    }
    snprintf(msg, n, "VM '%s' nao existe", s);
    return NULL;
}

HvStatus hv_pause(Hv *hv, const char *name, char *msg, size_t n)
{
    Vcb *vm = find_vm(hv, name, msg, n);
    if (!vm)
        return HV_ERR_NOTFOUND;
    VmState s = vm_state(vm);
    if (s != VM_READY && s != VM_RUNNING) {
        snprintf(msg, n, "pause recusado: %s esta em %s (so READY ou RUNNING)", vm_name(vm),
                 vm_state_name(s));
        return HV_ERR_INVALID;
    }
    int c = hv_find_cpu(hv, vm);
    if (c >= 0) { /* RF-SCH-06 */
        cpu_switch_out(hv->cpu, c);
        hv->preempt[c] = false;
        sched_on_block(hv->sched, vm, hv->clock);
    } else {
        sched_remove(hv->sched, vm);
    }
    vm_set_state(vm, VM_PAUSED, hv->clock);
    snprintf(msg, n, "%s pausada", vm_name(vm));
    return HV_OK;
}

HvStatus hv_resume(Hv *hv, const char *name, char *msg, size_t n)
{
    Vcb *vm = find_vm(hv, name, msg, n);
    if (!vm)
        return HV_ERR_NOTFOUND;
    if (vm_state(vm) != VM_PAUSED) {
        snprintf(msg, n, "resume recusado: %s nao esta pausada", vm_name(vm));
        return HV_ERR_INVALID;
    }
    vm_set_state(vm, VM_READY, hv->clock);
    sched_enqueue(hv->sched, vm, hv->clock);
    snprintf(msg, n, "%s voltou para READY", vm_name(vm));
    return HV_OK;
}

HvStatus hv_kill(Hv *hv, const char *name, char *msg, size_t n)
{
    Vcb *vm = find_vm(hv, name, msg, n);
    if (!vm)
        return HV_ERR_NOTFOUND;
    if (vm_state(vm) == VM_TERMINATED) {
        snprintf(msg, n, "%s ja terminou", vm_name(vm));
        return HV_ERR_INVALID;
    }
    hv_stop_vm(hv, vm, EXIT_KILLED);
    hv_check_end(hv);
    snprintf(msg, n, "%s encerrada (KILLED)", vm_name(vm));
    return HV_OK;
}

bool hv_ext_command(Hv *hv, const char *cmd, FILE *out)
{
    return ext_command(hv->ext, cmd, out);
}

const char *hv_ext_help(void)
{
    return ext_help();
}
