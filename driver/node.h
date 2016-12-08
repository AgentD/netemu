#ifndef NODE_H
#define NODE_H


#include "../cfg.h"


typedef struct cfg_node cfg_node;
typedef struct cfg_node_port cfg_node_port;
typedef struct cfg_iptables cfg_iptables;
typedef struct cfg_node_argvec cfg_node_argvec;


struct cfg_node_argvec {
	char **argv;
	int argc;

	cfg_node_argvec *next;
};

struct cfg_node_port {
	char name[MAX_NAME + 1];
	char **addresses;
	size_t num_addresses;
	unsigned int connected:1;
	cfg_node *owner;

	cfg_node_port *next;
};

struct cfg_node {
	char name[MAX_NAME + 1];
	unsigned int allow_forward:1;

	cfg_node_port *ports;
	cfg_node_argvec *iptables;
	cfg_node_argvec *routes;

	cfg_node *next;
};


cfg_node *node_find(const char *name);

cfg_node_port *node_find_port(cfg_node *node, const char *name);

int node_configure_port(cfg_node_port *port);

#endif /* NODE_H */

