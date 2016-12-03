#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include "driver.h"
#include "cable.h"
#include "netns.h"
#include "cfg.h"


static cfg_cable *cables = NULL;


static cfg_cable *create_cable(parse_ctx_t *ctx, int lineno, void *parent)
{
	cfg_cable *cable = calloc(1, sizeof(*cable));
	(void)ctx;

	assert(parent == NULL);

	if (!cable)
		fprintf(stderr, "%d: out of memory!\n", lineno);

	cable->next = cables;
	cables = cable;
	return cable;
}

static cfg_node_port *add_port(parse_ctx_t *ctx, int lineno, cfg_cable *cable)
{
	char node[MAX_NAME + 1], port[MAX_NAME + 1];
	cfg_node_port *p;
	cfg_token_t tk;
	cfg_node *n;
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

	if (cable->upper) {
		if (cable->lower) {
			fprintf(stderr, "%d: cable cannot have more than "
				"two ends\n", lineno);
			return NULL;
		}
		cable->lower = p;
	} else {
		cable->upper = p;
	}

	p->connected = 1;
	return p;
fail_errno:
	fprintf(stderr, "%d: %s!\n", lineno, strerror(errno));
	return NULL;
}

static void cables_cleanup(void)
{
	cfg_cable *cable;

	while (cables != NULL) {
		cable = cables;
		cables = cable->next;

		free(cable);
	}
}

static int cable_drv_start(driver_t *drv)
{
	cfg_node_port *upper, *lower;
	cfg_cable *cable;
	size_t i;
	(void)drv;

	for (cable = cables; cable != NULL; cable = cable->next) {
		upper = cable->upper;
		lower = cable->lower;

		if (upper == NULL || lower == NULL)
			continue;

		/* delete port X of node A */
		netns_run(lower->owner->name, "ip link set dev %s down",
						lower->name);

		netns_run(NULL, "ip link set dev %s-%s down",
				lower->owner->name, lower->name);

		netns_run(NULL, "ip link del %s-%s",
				lower->owner->name, lower->name);

		/* move outside end of port Y of node B into node A */
		netns_run(NULL, "ip link set %s-%s netns %s",
				upper->owner->name, upper->name,
				lower->owner->name);

		/* rename port Y into X */
		netns_run(lower->owner->name, "ip link set %s-%s name %s",
			upper->owner->name, upper->name, lower->name);

		netns_run(lower->owner->name, "ip link set dev %s up",
						lower->name);

		/* reconfigure */
		for (i = 0; i < lower->num_addresses; ++i) {
			netns_run(lower->owner->name, "ip addr add %s dev %s",
				lower->addresses[i], lower->name);
		}
	}
	return 0;
}

static int cable_drv_gen_graph(driver_t *drv)
{
	cfg_node_port *upper, *lower;
	cfg_cable *cable;
	size_t i;
	(void)drv;

	for (cable = cables; cable != NULL; cable = cable->next) {
		upper = cable->upper;
		lower = cable->lower;

		if (upper == NULL || lower == NULL)
			continue;

		printf("%s -- %s [taillabel=\"%s",
			lower->owner->name, upper->owner->name,
			lower->name);

		for (i = 0; i < lower->num_addresses; ++i) {
			printf("\\n%s", lower->addresses[i]);
		}

		printf("\", headlabel=\"%s", upper->name);

		for (i = 0; i < lower->num_addresses; ++i) {
			printf("\\n%s", upper->addresses[i]);
		}

		printf("\"];\n");
	}
	return 0;
}

static parser_token_t cable_tokens[] = {
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

static parser_token_t cable_block = {
	.keyword = "cable",
	.flags = FLAG_ARG_EXACT,
	.argcount = 0,
	.children = cable_tokens,
	.deserialize = (deserialize_fun_t)create_cable,
};

static driver_t cable_drv = {
	.start = cable_drv_start,
	.stop = NULL,
	.gen_graph = cable_drv_gen_graph,
};

EXPORT_PARSER(cable_block)
EXPORT_CLEANUP_HANDLER(cables_cleanup)
EXPORT_DRIVER(cable_drv, DRV_PRIORITY_CONNECTION)