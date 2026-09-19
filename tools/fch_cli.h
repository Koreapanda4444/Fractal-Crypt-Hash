#ifndef FCH_CLI_H
#define FCH_CLI_H

#include <stdio.h>

int fch_cli_run(
	int argc,
	char **argv,
	FILE *input,
	FILE *output,
	FILE *error
);

#endif
