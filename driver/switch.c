#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include "../driver.h"
#include "../netns.h"
#include "../cfg.h"
#include "switch.h"


static cfg_switch *switches = NULL;


static cfg_switch *create_switch(parse_ctx_t *ctx, int lineno, void *parent)
{
	cfg_switch *sw = calloc(1, sizeof(*sw));
	(void)ctx;

	assert(parent == NULL);

	if (!sw)
		fprintf(stderr, "%d: out of memory!\n", lineno);

	sw->next = switches;
	switches = sw;
	return sw;
}

static cfg_node_port *add_port(parse_ctx_t *ctx, int lineno, cfg_switch *sw)
{
	char node[MAX_NAME + 1], port[MAX_NAME + 1];
	size_t new_count;
	cfg_node_port *p;
	cfg_token_t tk;
	cfg_node *n;
	void *new;
	int ret;

	/* get arguments */
	ret = cfg_next_token(ctx, &tk, 0);
	if (ret < 0)
		goto fail_errno;
	assert(ret > 0 && tk.id == TK_ARG);

	if (cfg_get_arg(ctx, node, sizeof(node)))
		return NULL;

	ret = cfg_next_token(ctx, &tk, 0);
	if (ret < 0)
		goto fail_errno;
	assert(ret > 0 && tk.id == TK_ARG);

	if (cfg_get_arg(ctx, port, sizeof(port)))
		return NULL;

	/* find node port */
	n = node_find(node);
	if (!n) {
		fprintf(stderr, "%d: node '%s' does not exist\n",
			lineno, node);
		return NULL;
	}

	p = node_find_port(n, port);
	if (!p) {
		fprintf(stderr, "%d: node '%s' has no port named '%s'\n",
			lineno, node, port);
		return NULL;
	}

	if (p->connected) {
		fprintf(stderr, "%d: port '%s' on node '%s' is already "
			"connected\n", lineno, port, node);
		return NULL;
	}

	if (sw->max_connected == sw->num_connected) {
		new_count = sw->max_connected ? sw->max_connected * 2 : 10;

		new = realloc(sw->connected,
				new_count * sizeof(cfg_node_port*));
		if (!new)
			goto fail_errno;

		sw->connected = new;
		sw->max_connected = new_count;
	}

	sw->connected[sw->num_connected++] = p;
	p->connected = 1;
	return p;
fail_errno:
	fprintf(stderr, "%d: %s!\n", lineno, strerror(errno));
	return NULL;
}

static void switches_cleanup(void)
{
	cfg_switch *sw;

	while (switches != NULL) {
		sw = switches;
		switches = sw->next;

		free(sw->connected);
		free(sw);
	}
}

static int switch_drv_start(driver_t *drv)
{
	unsigned int i = 0;
	cfg_node_port *p;
	cfg_switch *sw;
	size_t j;
	(void)drv;

	for (sw = switches; sw != NULL; sw = sw->next, ++i) {
		netns_run(NULL, "ip link add name switch%d type bridge", i);
		netns_run(NULL, "ip link set dev switch%d up", i);

		for (j = 0; j < sw->num_connected; ++j) {
			p = sw->connected[j];

			netns_run(NULL,
				  "ip link set dev %s-%s master switch%d",
				  p->owner->name, p->name, i);
		}
	}
	return 0;
}

static int switch_drv_stop(driver_t *drv)
{
	unsigned int i = 0;
	cfg_node_port *p;
	cfg_switch *sw;
	size_t j;
	(void)drv;

	for (sw = switches; sw != NULL; sw = sw->next, ++i) {
		for (j = 0; j < sw->num_connected; ++j) {
			p = sw->connected[j];

			netns_run(NULL, "ip link set dev %s-%s nomaster",
					p->owner->name, p->name);
		}

		netns_run(NULL, "ip link set switch%d down", i);
		netns_run(NULL, "ip link delete dev switch%d", i);
	}
	return 0;
}

static int switch_drv_gen_graph(driver_t *drv)
{
	unsigned int i = 0;
	cfg_node_port *p;
	cfg_switch *sw;
	size_t j, k;
	(void)drv;

	for (sw = switches; sw != NULL; sw = sw->next, ++i) {
		printf("switch%d [shape = box];\n", i);

		for (j = 0; j < sw->num_connected; ++j) {
			p = sw->connected[j];

			printf("%s -- switch%d [taillabel=\"%s",
				p->owner->name, i, p->name);

			for (k = 0; k < p->num_addresses; ++k) {
				printf("\\n%s", p->addresses[k]);
			}

			printf("\"];\n");
		}
	}
	return 0;
}

static parser_token_t switch_tokens[] = {
	{
		.keyword = "port",
		.flags = FLAG_ARG_EXACT,
		.argcount = 2,
		.argfun = cfg_check_name_arg,
		.deserialize = (deserialize_fun_t)add_port,
	}, {
		.keyword = NULL,
	},
};

static parser_token_t switch_block = {
	.keyword = "switch",
	.flags = FLAG_ARG_EXACT,
	.argcount = 0,
	.children = switch_tokens,
	.deserialize = (deserialize_fun_t)create_switch,
};

static driver_t switch_drv = {
	.start = switch_drv_start,
	.stop = switch_drv_stop,
	.gen_graph = switch_drv_gen_graph,
};

EXPORT_PARSER(switch_block)
EXPORT_CLEANUP_HANDLER(switches_cleanup)
EXPORT_DRIVER(switch_drv, DRV_PRIORITY_SWITCH)
