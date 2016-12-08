#ifndef SWITCH_H
#define SWITCH_H

#include "node.h"

typedef struct cfg_switch {
	cfg_node_port** connected;
	size_t num_connected;
	size_t max_connected;

	struct cfg_switch *next;
} cfg_switch;

#endif /* SWITCH_H */

