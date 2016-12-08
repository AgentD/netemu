#ifndef CABLE_H
#define CABLE_H

#include "node.h"

#define MAX_LIMIT_STR 32

typedef struct cfg_cable {
	cfg_node_port *upper;
	cfg_node_port *lower;

	char uplimit[MAX_LIMIT_STR];
	char downlimit[MAX_LIMIT_STR];

	struct cfg_cable *next;
} cfg_cable;

#endif /* CABLE_H */

