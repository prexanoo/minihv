#ifndef CLI_CLI_H
#define CLI_CLI_H

#include <stdio.h>

#include "hv/hv.h"

/* RF-CLI-02: laco do modo interativo (prompt "minihv> ") */
void cli_run(Hv *hv, FILE *in, FILE *out);

void cli_print_help(FILE *out);

#endif
