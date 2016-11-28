#ifndef CABLE_H
#define CABLE_H

#include "node.h"

typedef struct cfg_cable {
	cfg_node_port *upper;
	cfg_node_port *lower;

	struct cfg_cable *next;
} cfg_cable;

#endif /* CABLE_H */

