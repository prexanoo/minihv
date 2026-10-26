#include "cli/cli.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define CLI_LINE 256
#define MSG_LEN 160

typedef struct CliState {
    Hv *hv;
    FILE *out;
    bool reported; /* relatorio final ja foi impresso */
} CliState;

void cli_print_help(FILE *out)
{
    fputs("comandos:\n"
          "  step              executa 1 tick\n"
          "  run [n]           executa n ticks (sem n: ate o fim)\n"
          "  ps                lista as VMs\n"
          "  mem               mostra particoes e buracos da memoria\n"
          "  io                mostra os dispositivos e as filas\n"
          "  stats             relatorio parcial\n"
          "  pause <vm>        pausa uma VM em READY ou RUNNING\n"
          "  resume <vm>       volta uma VM pausada para READY\n"
          "  kill <vm>         encerra uma VM\n",
          out);
    fputs(hv_ext_help(), out);
    fputs("  help              esta ajuda\n"
          "  quit              sai\n"
          "  (<vm> pode ser o nome ou o id)\n",
          out);
}

static void check_finished(CliState *cs)
{
    if (!hv_finished(cs->hv) || cs->reported)
        return;
    fprintf(cs->out, "simulacao terminada no tick %llu\n",
            (unsigned long long)hv_clock(cs->hv));
    hv_finish(cs->hv, cs->out);
    cs->reported = true;
}

static void cmd_run(CliState *cs, const char *arg)
{
    if (hv_finished(cs->hv)) {
        fputs("a simulacao ja terminou\n", cs->out);
        return;
    }
    char *end;
    long n = arg ? strtol(arg, &end, 10) : 0;
    if (arg && (*end != '\0' || n <= 0)) {
        fputs("uso: run [n], com n > 0\n", cs->out);
        return;
    }
    hv_run(cs->hv, (uint64_t)n);
    fprintf(cs->out, "t=%llu\n", (unsigned long long)hv_clock(cs->hv));
    check_finished(cs);
}

static void cmd_vm(CliState *cs, const char *cmd, const char *arg)
{
    char msg[MSG_LEN];
    if (!arg) {
        fprintf(cs->out, "uso: %s <vm>\n", cmd);
        return;
    }
    if (strcmp(cmd, "pause") == 0)
        hv_pause(cs->hv, arg, msg, sizeof msg);
    else if (strcmp(cmd, "resume") == 0)
        hv_resume(cs->hv, arg, msg, sizeof msg);
    else
        hv_kill(cs->hv, arg, msg, sizeof msg);
    fprintf(cs->out, "%s\n", msg);
    check_finished(cs);
}

/* devolve false quando o usuario pede para sair */
static bool dispatch(CliState *cs, const char *cmd, const char *arg)
{
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0)
        return false;
    if (strcmp(cmd, "step") == 0)
        cmd_run(cs, "1");
    else if (strcmp(cmd, "run") == 0)
        cmd_run(cs, arg);
    else if (strcmp(cmd, "ps") == 0)
        hv_print_ps(cs->hv, cs->out);
    else if (strcmp(cmd, "mem") == 0)
        hv_print_mem(cs->hv, cs->out);
    else if (strcmp(cmd, "io") == 0)
        hv_print_io(cs->hv, cs->out);
    else if (strcmp(cmd, "stats") == 0)
        hv_print_report(cs->hv, cs->out);
    else if (!strcmp(cmd, "pause") || !strcmp(cmd, "resume") || !strcmp(cmd, "kill"))
        cmd_vm(cs, cmd, arg);
    else if (strcmp(cmd, "help") == 0)
        cli_print_help(cs->out);
    else if (!hv_ext_command(cs->hv, cmd, cs->out)) {
        fprintf(cs->out, "comando invalido: '%s'\n", cmd); /* RF-CLI-04 */
        cli_print_help(cs->out);
    }
    return true;
}

static char *next_token(char **p)
{
    while (isspace((unsigned char)**p))
        (*p)++;
    if (**p == '\0')
        return NULL;
    char *ini = *p;
    while (**p && !isspace((unsigned char)**p))
        (*p)++;
    if (**p)
        *(*p)++ = '\0';
    return ini;
}

void cli_run(Hv *hv, FILE *in, FILE *out)
{
    CliState cs = {hv, out, false};
    char line[CLI_LINE];
    fputs("MiniHV modo interativo. Digite 'help' para ver os comandos.\n", out);
    check_finished(&cs);
    for (;;) {
        fputs("minihv> ", out);
        fflush(out);
        if (!fgets(line, sizeof line, in))
            break; /* EOF funciona como quit */
        char *p = line;
        char *cmd = next_token(&p);
        if (!cmd)
            continue;
        char *arg = next_token(&p);
        if (!dispatch(&cs, cmd, arg))
            break;
    }
}
