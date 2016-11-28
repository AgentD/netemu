#include <sys/wait.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "daemon.h"
#include "netns.h"


daemon_t *daemons = NULL;


int launch_daemon(const char *node, const char *cmdline)
{
	daemon_t *daemon;

	daemon = calloc(1, sizeof(*daemon));
	if (!daemon) {
		perror("calloc");
		return -1;
	}

	daemon->node = strdup(node);
	if (!daemon->node) {
		perror("strdup");
		goto fail_free;
	}

	daemon->cmdline = strdup(cmdline);
	if (!daemon->cmdline) {
		perror("strdup");
		goto fail_str1;
	}

	daemon->pid = netns_launch(node, "%s", cmdline);
	if (daemon->pid == -1)
		goto fail_str2;

	daemon->next = daemons;
	daemons = daemon;
	return 0;
fail_str2:
	free(daemon->cmdline);
fail_str1:
	free(daemon->node);
fail_free:
	free(daemon);
	return -1;
}

static pid_t wait_pid_ms(pid_t pid, int* status, unsigned long timeout)
{
	struct timeval before, after;
	unsigned long delta;
	struct timespec ts;
	sigset_t mask;
	int result;
	pid_t ret;

	if (!timeout)
		return waitpid(pid, status, 0);

	while (timeout) {
		ret = waitpid(pid, status, WNOHANG);

		if (ret!=0)
			return ret;

		/* setup timeout structure and signal mask */
		ts.tv_sec  = timeout / 1000;
		ts.tv_nsec = ((long)timeout - ts.tv_sec*1000L)*1000000L;

		sigemptyset( &mask );
		sigaddset( &mask, SIGCHLD );

		/* wait for a signal */
		gettimeofday( &before, NULL );
		result = pselect( 0, NULL, NULL, NULL, &ts, &mask );

		if (result==0)
			return 0;
		if (result==-1 && errno!=EINTR)
			return -1;

		/* subtract elapsed time from timeout */
		gettimeofday( &after, NULL );

		delta  = (after.tv_sec  - before.tv_sec )*1000;
		delta += (after.tv_usec - before.tv_usec)/1000;
		timeout = (delta >= (unsigned long)timeout) ? 0 : (timeout - delta);
	}

	return 0;
}

static void kill_daemons(int signo)
{
	daemon_t *daemon;

	for (daemon = daemons; daemon != NULL; daemon = daemon->next) {
		if (daemon->pid == -1)
			continue;

		if (signo == SIGKILL) {
			fprintf(stderr, "sending SIGKILL to daemon on node %s (%s)\n",
					daemon->node, daemon->cmdline);
		}

		kill(daemon->pid, signo);
	}
}

static void wait_daemons(unsigned long timeout)
{
	daemon_t *daemon;

	for (daemon = daemons; daemon != NULL; daemon = daemon->next) {
		if (daemon->pid == -1)
			continue;

		if (wait_pid_ms(daemon->pid, NULL, timeout) == daemon->pid)
			daemon->pid = -1;
	}
}

void check_daemons(void)
{
	daemon_t *daemon;

	for (daemon = daemons; daemon != NULL; daemon = daemon->next) {
		if (daemon->pid == -1)
			continue;

		if (waitpid(daemon->pid, NULL, WNOHANG) == daemon->pid) {
			daemon->pid = -1;

			fprintf(stderr, "daemon on node %s terminated (%s)\n",
					daemon->node, daemon->cmdline);
		}
	}
}

void stop_daemons(void)
{
	daemon_t *daemon;

	kill_daemons(SIGTERM);
	wait_daemons(5000);

	kill_daemons(SIGINT);
	wait_daemons(5000);

	kill_daemons(SIGKILL);
	wait_daemons(0);

	while (daemons) {
		daemon = daemons;
		daemons = daemons->next;

		free(daemon->node);
		free(daemon->cmdline);
		free(daemon);
	}
}

