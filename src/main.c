#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli/cli.h"
#include "hv/hv.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "uso: %s [-c arquivo.conf] [-i] [-p POLITICA]\n"
            "  -c  arquivo de configuracao (padrao conf/hv.conf)\n"
            "  -i  modo interativo\n"
            "  -p  sobrescreve a politica do arquivo (CREDIT ou FCFS)\n",
            prog);
}

int main(int argc, char **argv)
{
    HvOptions opt = {NULL, NULL, stdout};
    int interativo = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            opt.config_path = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            opt.policy_override = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0) {
            interativo = 1;
        } else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    Hv *hv = NULL;
    if (hv_create(&opt, &hv) != HV_OK)
        return EXIT_FAILURE;

    if (interativo) {
        cli_run(hv, stdin, stdout);
    } else { /* RF-CLI-01: modo batch */
        hv_run(hv, 0);
        hv_finish(hv, stdout);
    }
    hv_destroy(hv);
    return EXIT_SUCCESS;
}
