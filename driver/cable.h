#ifndef CABLE_H
#define CABLE_H

#include "../cfg.h"
#include "node.h"

#define MAX_LIMIT_STR 32

typedef struct cfg_cable {
	cfg_node_port *upper;
	cfg_node_port *lower;

	bandwidth_t uplimit;
	bandwidth_t downlimit;

	double loss;
	double corrupt;
	double duplicate;

	struct cfg_cable *next;
} cfg_cable;

#endif /* CABLE_H */

