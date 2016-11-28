#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

#include "driver.h"
#include "cfg.h"


extern const char *__progname;


static void usage(int status)
{
	FILE *out = status==EXIT_SUCCESS ? stdout : stderr;
	fprintf(out,
		"usage: %s <configfile> [start|stop|graph]\n\n", __progname);
	fputs("  start - Create the network described by the file\n", out);
	fputs("  stop  - Destroy the network described by the file\n", out);
	fputs("  graph - Generate a dot tool script from the file\n", out);
	exit(status);
}

int main(int argc, char **argv)
{
	int i, cmd, ret;

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			usage(EXIT_SUCCESS);
		}
	}

	if (argc != 3)
		usage(EXIT_FAILURE);

	if (!strcmp(argv[2], "start"))
		cmd = CMD_START;
	else if (!strcmp(argv[2], "stop"))
		cmd = CMD_STOP;
	else if (!strcmp(argv[2], "graph"))
		cmd = CMD_GRAPH;
	else
		usage(EXIT_FAILURE);

	if (cfg_read(argv[1]))
		return EXIT_FAILURE;

	if (driver_run(cmd))
		goto out;

	ret = EXIT_SUCCESS;
out:
	cfg_cleanup();
	cfg_unregister_keywords();
	cfg_unregister_parser_tokens();
	return ret;
}

