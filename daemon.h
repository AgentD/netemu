#ifndef DAEMON_H
#define DAEMON_H


#include <sys/types.h>


typedef struct daemon_t {
	pid_t pid;
	char *node;
	char *cmdline;
	struct daemon_t *next;
} daemon_t;


extern daemon_t *daemons;


int launch_daemon(const char *node, const char *cmdline);

void check_daemons(void);

void stop_daemons(void);

#endif /* DAEMON_H */

