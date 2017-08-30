#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>

#include "daemon.h"
#include "netns.h"

#define DAEMON_CHECK_INTERVAL 2

#define MODE_MUSTWORK 0
#define MODE_MUSTFAIL 1


extern const char *__progname;


static int mode = MODE_MUSTWORK;
static char *onerror = NULL;
static int lineno;


static const char *pingcmd = "ping -i 0.2 -c 4 -W 2";
static pid_t mainpid;


static void sighandler(int signal)
{
	if (getpid() != mainpid)
		return;

	if (signal == SIGALRM) {
		check_daemons();
		alarm(DAEMON_CHECK_INTERVAL);
	}
}

static void usage(int status)
{
	FILE *out = status==EXIT_SUCCESS ? stdout : stderr;
	fprintf(out, "usage: %s <testscript>\n", __progname);
	exit(status);
}

static int run_command(char *cmd)
{
	int ret = 0;
	char *node;

	/* extract node name */
	if (!isalpha(*cmd))
		goto fail_name;

	for (node = cmd; isalnum(*cmd); ++cmd) { }

	if (!(*cmd))
		goto empty;
	if (!isspace(*cmd))
		goto fail_name;

	/* extract command line */
	for (*(cmd++) = '\0'; isspace(*cmd); ++cmd) { }

	if (!(*cmd))
		goto empty;

	/* run command */
	if (!strncmp(cmd, "ping", 4) && isspace(cmd[4])) {
		for (cmd += 4; isspace(*cmd); ++cmd) { }

		printf("%s: %s %s\n", node, pingcmd, cmd);
		ret = netns_run(node, "%s %s", pingcmd, cmd);
	} else if (!strncmp(cmd, "daemon", 6) && isspace(cmd[6])) {
		for (cmd += 6; isspace(*cmd); ++cmd) { }

		if (launch_daemon(node, cmd) != 0)
			return -1;

		printf("%s: daemon %s\n", node, cmd);
	} else {
		printf("%s: %s\n", node, cmd);
		ret = netns_run(node, "%s", cmd);
	}

	return ret;
empty:
	fprintf(stderr, "%d: ignoring empty command for node '%s'\n",
			lineno, node);
	return 0;
fail_name:
	fprintf(stderr, "%d: illegal node name\n", lineno);
	return -1;
}

int main(int argc, char **argv)
{
	int i, ret, status = EXIT_FAILURE;
	char buffer[512], *cmd, *end;
	FILE *in;

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
			usage(EXIT_SUCCESS);
	}

	if (argc != 2)
		usage(EXIT_FAILURE);

	/* open input file */
	in = fopen(argv[1], "r");
	if (!in) {
		perror(argv[1]);
		return EXIT_FAILURE;
	}

	fcntl(fileno(in), F_SETFD, FD_CLOEXEC);

	/* init */
	onerror = strdup("test failed");
	if (!onerror) {
		perror("strdup");
		goto out;
	}

	mainpid = getpid();

	signal(SIGALRM, sighandler);
	alarm(DAEMON_CHECK_INTERVAL);

	/* process file */
	for (lineno = 1; fgets(buffer, sizeof(buffer), in); ++lineno) {
		for (cmd = buffer; isspace(*cmd); ++cmd) { }

		if (!(*cmd) || *cmd == '#')
			continue;

		for (end = cmd; *end && *end!='#'; ++end) { }

		--end;
		while (end > cmd && isspace(*end))
			--end;
		end[1] = '\0';

		if (!strlen(cmd))
			continue;

		if (!strcmp(cmd, "mustwork")) {
			mode = MODE_MUSTWORK;
		} else if (!strcmp(cmd, "mustfail")) {
			mode = MODE_MUSTFAIL;
		} else if (!strncmp(cmd, "onerror", 7) && isspace(cmd[7])) {
			for (cmd += 7; isspace(*cmd); ++cmd) { }

			onerror = strdup(cmd);
			if (!onerror) {
				perror("strdup");
				goto out;
			}
		} else {
			ret = run_command(cmd);

			if (ret == 0 && mode == MODE_MUSTFAIL)
				ret = -1;
			else if (ret != 0 && mode == MODE_MUSTWORK)
				ret = -1;
			else
				ret = 0;

			if (ret != 0) {
				fprintf(stderr, "%d: %s\n", lineno, onerror);
				goto out;
			}
		}
	}

	status = EXIT_SUCCESS;
out:
	alarm(0);
	fclose(in);
	free(onerror);
	stop_daemons();
	return status;
}

