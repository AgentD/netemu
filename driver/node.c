#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>

#include "../driver.h"
#include "../netns.h"
#include "../cfg.h"
#include "node.h"

static cfg_node *nodes = NULL;

static int node_port_arg(parse_ctx_t *ctx, int index, int lineno)
{
	if (index == 0)
		return cfg_check_name_arg(ctx, index, lineno);

	return cfg_check_ip_addr_arg(ctx, index, lineno);
}

static int node_lanfwd_arg(parse_ctx_t *ctx, int index, int lineno)
{
	if (index < 2)
		return cfg_check_name_arg(ctx, index, lineno);

	return cfg_check_ip_addr_arg(ctx, index, lineno);
}

static cfg_node *create_node(parse_ctx_t *ctx, int lineno, void *parent)
{
	cfg_node *node = calloc(1, sizeof(*node));
	cfg_token_t tk;
	cfg_node *n;
	int ret;

	assert(parent == NULL);

	if (!node)
		goto fail_errno;

	ret = cfg_next_token(ctx, &tk, 0);
	if (ret < 0)
		goto fail_errno;

	assert(ret > 0 && tk.id == TK_ARG);

	ret = cfg_get_arg(ctx, node->name, MAX_NAME + 1);
	if (ret < 0)
		goto fail;

	assert(ret == 0);

	for (n = nodes; n != NULL; n = n->next) {
		if (!strcmp(n->name, node->name)) {
			fprintf(stderr, "%d: node '%s' redefined\n",
					lineno, node->name);
			goto fail;
		}
	}

	node->next = nodes;
	nodes = node;
	return node;
fail_errno:
	fprintf(stderr, "%d: %s!\n", lineno, strerror(errno));
fail:
	free(node);
	return NULL;
}

static cfg_node_port *add_port(parse_ctx_t *ctx, int lineno, cfg_node *node)
{
	char name[MAX_NAME + 1];
	cfg_node_port *p;
	cfg_token_t tk;
	int ret, count;

	if ((ret = cfg_next_token(ctx, &tk, 0)) < 0)
		return NULL;

	assert(ret > 0 && tk.id == TK_ARG);
	if (cfg_get_arg(ctx, name, sizeof(name)))
		return NULL;

	for (p = node->ports; p != NULL; p = p->next) {
		if (!strcmp(p->name, name)) {
			fprintf(stderr, "%d: port '%s' redefined\n",
				lineno, name);
			return NULL;
		}
	}

	p = calloc(1, sizeof(*p));
	if (!p)
		goto fail_alloc;

	strcpy(p->name, name);

	p->addresses = cfg_read_argvec(ctx, &count, lineno);
	if (!p->addresses)
		goto fail_free;

	p->num_addresses = count;
	p->owner = node;
	p->next = node->ports;
	node->ports = p;
	return p;
fail_alloc:
	fprintf(stderr, "%d: out of memory\n", lineno);
fail_free:
	free(p);
	return NULL;
}

static cfg_node_argvec *add_route(parse_ctx_t *ctx, int lineno, cfg_node *node)
{
	cfg_node_argvec *vec = calloc(1, sizeof(*vec));
	cfg_node_argvec *v;

	if (!vec)
		goto fail_alloc;

	vec->argv = cfg_read_argvec(ctx, &(vec->argc), lineno);
	if (!vec->argv)
		goto fail_alloc;

	if (node->routes != NULL) {
		for (v = node->routes; v->next != NULL; v = v->next) {
		}

		v->next = vec;
	} else {
		node->routes = vec;
	}
	return vec;
fail_alloc:
	fprintf(stderr, "%d: out of memory\n", lineno);
	free(vec);
	return NULL;
}

static cfg_node_lan_fwd *add_lan_forward(parse_ctx_t *ctx, int lineno,
					cfg_node *node)
{
	char name[MAX_NAME + 1];
	cfg_node_lan_fwd *fwd;
	cfg_node_port *p;
	cfg_token_t tk;
	int ret, count;

	/* get node port */
	if ((ret = cfg_next_token(ctx, &tk, 0)) < 0)
		return NULL;

	assert(ret > 0 && tk.id == TK_ARG);
	if (cfg_get_arg(ctx, name, sizeof(name)))
		return NULL;

	p = node_find_port(node, name);

	if (!p) {
		fprintf(stderr, "%d: cannot find port '%s'\n", lineno, name);
		return NULL;
	}

	/* get external port name */
	if ((ret = cfg_next_token(ctx, &tk, 0)) < 0)
		return NULL;

	assert(ret > 0 && tk.id == TK_ARG);
	if (cfg_get_arg(ctx, name, sizeof(name)))
		return NULL;

	/* create forwarding rule */
	fwd = calloc(1, sizeof(*fwd));
	if (!fwd) {
		fprintf(stderr, "%d: out of memory\n", lineno);
		return NULL;
	}

	/* get addresses to assign to external veth pair device */
	fwd->addresses = cfg_read_argvec(ctx, &count, lineno);
	if (!fwd->addresses) {
		free(fwd);
		fprintf(stderr, "%d: out of memory\n", lineno);
		return NULL;
	}

	/* store changes */
	strcpy(fwd->external, name);
	fwd->port = p;
	fwd->num_addresses = count;

	p->connected = 1;

	fwd->next = node->fwd;
	node->fwd = fwd;
	return fwd;
}

static cfg_node_argvec *add_iptables(parse_ctx_t *ctx, int lineno,
					cfg_node *node)
{
	cfg_node_argvec *vec = calloc(1, sizeof(*vec));
	cfg_node_argvec *v;

	if (!vec)
		goto fail_alloc;

	vec->argv = cfg_read_argvec(ctx, &(vec->argc), lineno);
	if (!vec->argv)
		goto fail_alloc;

	if (node->iptables != NULL) {
		for (v = node->iptables; v->next != NULL; v = v->next) {
		}

		v->next = vec;
	} else {
		node->iptables = vec;
	}
	return vec;
fail_alloc:
	fprintf(stderr, "%d: out of memory\n", lineno);
	free(vec);
	return NULL;
}

static cfg_node *set_allow_forward(parse_ctx_t *ctx, int lineno,
				cfg_node *node)
{
	(void)ctx; (void)lineno;
	node->allow_forward = 1;
	return node;
}

cfg_node *node_find(const char *name)
{
	cfg_node *n;

	for (n = nodes; n != NULL; n = n->next) {
		if (!strcmp(n->name, name))
			break;
	}

	return n;
}

cfg_node_port *node_find_port(cfg_node *node, const char *name)
{
	cfg_node_port *p;

	for (p = node->ports; p != NULL; p = p->next) {
		if (!strcmp(p->name, name))
			break;
	}

	return p;
}

static void nodes_cleanup(void)
{
	cfg_node_lan_fwd *fwd;
	cfg_node_argvec *v;
	cfg_node_port *p;
	cfg_node *n;
	size_t i;
	int j;

	while (nodes != NULL) {
		n = nodes;
		nodes = n->next;

		while (n->ports != NULL) {
			p = n->ports;
			n->ports = p->next;

			for (i = 0; i < p->num_addresses; ++i)
				free(p->addresses[i]);

			free(p->addresses);
			free(p);
		}

		while (n->iptables != NULL) {
			v = n->iptables;
			n->iptables = v->next;

			for (j = 0; j < v->argc; ++j)
				free(v->argv[j]);

			free(v->argv);
			free(v);
		}

		while (n->routes != NULL) {
			v = n->routes;
			n->routes = v->next;

			for (j = 0; j < v->argc; ++j)
				free(v->argv[j]);

			free(v->argv);
			free(v);
		}

		while (n->fwd != NULL) {
			fwd = n->fwd;
			n->fwd = fwd->next;

			for (i = 0; i < fwd->num_addresses; ++i)
				free(fwd->addresses[i]);

			free(fwd->addresses);
			free(fwd);
		}

		free(n);
	}
}

int node_configure_port(cfg_node_port *p)
{
	cfg_node *n = p->owner;
	size_t i;

	netns_run(n->name, "ip link set dev %s up", p->name);

	for (i = 0; i < p->num_addresses; ++i) {
		netns_run(n->name, "ip addr add %s dev %s",
				p->addresses[i], p->name);
	}
	return 0;
}

static void setup_forward(const char *node, const char *external,
			const char *internal)
{
	netns_run(NULL, "iptables -t nat -A POSTROUTING -o %s -j MASQUERADE",
			external);

	netns_run(NULL, "iptables -A FORWARD -i %s -o %s-%s -m state "
			"--state RELATED,ESTABLISHED -j ACCEPT",
			external, node, internal);

	netns_run(NULL, "iptables -A FORWARD -i %s-%s -o %s -j ACCEPT",
			node, internal, external);
}

static void teardown_forward(const char *node, const char *external,
				const char *internal)
{
	netns_run(NULL, "iptables -t nat -D POSTROUTING -o %s -j MASQUERADE",
			external);

	netns_run(NULL, "iptables -D FORWARD -i %s -o %s-%s -m state "
			"--state RELATED,ESTABLISHED -j ACCEPT",
			external, node, internal);

	netns_run(NULL, "iptables -D FORWARD -i %s-%s -o %s -j ACCEPT",
			node, internal, external);
}

static int node_drv_start(driver_t *drv)
{
	cfg_node_lan_fwd *fwd;
	cfg_node_argvec *v;
	cfg_node_port *p;
	cfg_node *n;
	size_t i;
	(void)drv;

	for (n = nodes; n != NULL; n = n->next) {
		if (netns_add(n->name))
			return -1;

		for (p = n->ports; p != NULL; p = p->next) {
			netns_run(NULL, "ip link add %s-%s type veth "
					"peer name node-%s",
					n->name, p->name, p->name);
			netns_run(NULL, "ip link set node-%s netns %s",
					p->name, n->name);
			netns_run(NULL, "ip link set dev %s-%s up",
					n->name, p->name);

			netns_run(n->name, "ip link set node-%s name %s",
				p->name, p->name);

			node_configure_port(p);
		}

		netns_run(n->name, "ip link set dev lo up");

		if (n->allow_forward) {
			netns_run(n->name, "sysctl -w net.ipv4.ip_forward=1");
		}

		for (v = n->routes; v != NULL; v = v->next) {
			netns_run_argv(n->name, "ip route add",
					v->argc, v->argv);
		}

		for (v = n->iptables; v != NULL; v = v->next) {
			netns_run_argv(n->name, "iptables",
					v->argc, v->argv);
		}

		if (n->fwd)
			netns_run(NULL, "sysctl -w net.ipv4.ip_forward=1");

		for (fwd = n->fwd; fwd != NULL; fwd = fwd->next) {
			for (i = 0; i < fwd->num_addresses; ++i) {
				netns_run(NULL, "ip addr add %s dev %s-%s",
						fwd->addresses[i],
						n->name, fwd->port->name);
			}

			setup_forward(n->name, fwd->external, fwd->port->name);
		}
	}
	return 0;
}

static int node_drv_stop(driver_t *drv)
{
	cfg_node_lan_fwd *fwd;
	cfg_node_port *p;
	cfg_node *n;
	(void)drv;

	for (n = nodes; n != NULL; n = n->next) {
		if (n->fwd)
			netns_run(NULL, "sysctl -w net.ipv4.ip_forward=0");

		for (fwd = n->fwd; fwd != NULL; fwd = fwd->next) {
			teardown_forward(n->name, fwd->external,
					fwd->port->name);
		}

		for (p = n->ports; p != NULL; p = p->next) {
			netns_run(NULL, "ip link del %s-%s",
					n->name, p->name);
		}
		netns_delete(n->name);
	}
	return 0;
}

static int node_drv_gen_graph(driver_t *drv)
{
	cfg_node *n;
	(void)drv;

	for (n = nodes; n != NULL; n = n->next)
		printf("%s [shape = circle];\n", n->name);

	return 0;
}

static parser_token_t node_tokens[] = {
	{
		.keyword = "port",
		.flags = FLAG_ARG_MIN,
		.argcount = 1,
		.argfun = node_port_arg,
		.deserialize = (deserialize_fun_t)add_port,
	}, {
		.keyword = "route",
		.flags = FLAG_ARG_MIN,
		.argcount = 1,
		.deserialize = (deserialize_fun_t)add_route,
	}, {
		.keyword = "allowforward",
		.flags = FLAG_ARG_EXACT,
		.argcount = 0,
		.deserialize = (deserialize_fun_t)set_allow_forward,
	}, {
		.keyword = "iptables",
		.flags = FLAG_ARG_MIN,
		.argcount = 1,
		.deserialize = (deserialize_fun_t)add_iptables,
	}, {
		.keyword = "lanfwd",
		.flags = FLAG_ARG_MIN,
		.argcount = 2,
		.argfun = node_lanfwd_arg,
		.deserialize = (deserialize_fun_t)add_lan_forward,
	}, {
		.keyword = NULL,
	},
};

static parser_token_t node_block = {
	.keyword = "node",
	.flags = FLAG_ARG_EXACT,
	.argcount = 1,
	.argfun = cfg_check_name_arg,
	.children = node_tokens,
	.deserialize = (deserialize_fun_t)create_node,
};

static driver_t node_drv = {
	.start = node_drv_start,
	.stop = node_drv_stop,
	.gen_graph = node_drv_gen_graph,
};

EXPORT_PARSER(node_block)
EXPORT_CLEANUP_HANDLER(nodes_cleanup)
EXPORT_DRIVER(node_drv, DRV_PRIORITY_NODE)
