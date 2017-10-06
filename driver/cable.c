#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include "../driver.h"
#include "../netns.h"
#include "../cfg.h"
#include "cable.h"


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

static int check_cable_port(parse_ctx_t *ctx, int index, int lineno)
{
	char buffer[MAX_LIMIT_STR];

	if (index < 2)
		return cfg_check_name_arg(ctx, index, lineno);

	if (index == 2) {
		if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
			return -1;

		if (strlen(buffer) >= (sizeof(buffer) - 1)) {
			fprintf(stderr, "%d: bandwidth argument too big\n",
				lineno);
			return -1;
		}

		return cfg_parse_bandwidth(buffer, lineno, NULL);
	}

	fprintf(stderr, "%d: too many arguments\n", lineno);
	return -1;
}

static int check_delay(parse_ctx_t *ctx, int index, int lineno)
{
	char buffer[64];
	unsigned long l;
	(void)index;

	if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
		return -1;

	if (strlen(buffer) >= (sizeof(buffer) - 1)) {
		fprintf(stderr, "%d: argument too long\n", lineno);
		return -1;
	}

	return cfg_parse_time_ms(buffer, lineno, &l);
}

static int check_ratio(parse_ctx_t *ctx, int index, int lineno)
{
	char buffer[64];
	double x;
	(void)index;

	if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
		return -1;

	if (strlen(buffer) >= (sizeof(buffer) - 1)) {
		fprintf(stderr, "%d: argument too long\n", lineno);
		return -1;
	}

	return cfg_parse_ratio(buffer, lineno, &x);
}

static cfg_node_port *add_port(parse_ctx_t *ctx, int lineno, cfg_cable *cable)
{
	char node[MAX_NAME + 1], port[MAX_NAME + 1], limit[MAX_LIMIT_STR];
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

	/* bandwidth limit */
	ret = cfg_next_token(ctx, &tk, 1);
	if (ret < 0)
		goto fail_errno;
	assert(ret != 0);

	if (tk.id == TK_ARG) {
		ctx->readoff += sizeof(tk);
		if (cfg_get_arg(ctx, limit, sizeof(limit)))
			return NULL;
	} else {
		limit[0] = '\0';
	}

	/* store */
	if (cable->upper) {
		if (cable->lower) {
			fprintf(stderr, "%d: cable cannot have more than "
				"two ends\n", lineno);
			return NULL;
		}
		cable->lower = p;

		cfg_parse_bandwidth(limit, lineno, &cable->uplimit);
	} else {
		cable->upper = p;

		cfg_parse_bandwidth(limit, lineno, &cable->downlimit);
	}

	p->connected = 1;
	return p;
fail_errno:
	fprintf(stderr, "%d: %s!\n", lineno, strerror(errno));
	return NULL;
}

static int read_ratio(parse_ctx_t *ctx, int lineno, double *value)
{
	char buffer[64];
	cfg_token_t tk;
	int ret;

	ret = cfg_next_token(ctx, &tk, 0);
	if (ret < 0)
		goto fail_errno;
	assert(ret > 0 && tk.id == TK_ARG);

	if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
		return -1;

	return cfg_parse_ratio(buffer, lineno, value);
fail_errno:
	fprintf(stderr, "%d: %s!\n", lineno, strerror(errno));
	return -1;
}

static cfg_cable *add_delay(parse_ctx_t *ctx, int lineno, cfg_cable *cable)
{
	char buffer[64];
	cfg_token_t tk;
	int ret;

	ret = cfg_next_token(ctx, &tk, 0);
	if (ret < 0)
		goto fail_errno;
	assert(ret > 0 && tk.id == TK_ARG);

	if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
		return NULL;

	if (cfg_parse_time_ms(buffer, lineno, &cable->delay))
		return NULL;

	return cable;
fail_errno:
	fprintf(stderr, "%d: %s!\n", lineno, strerror(errno));
	return NULL;
}

static cfg_cable *add_loss(parse_ctx_t *ctx, int lineno, cfg_cable *cable)
{
	if (read_ratio(ctx, lineno, &cable->loss))
		return NULL;
	return cable;
}

static cfg_cable *add_corruption(parse_ctx_t *ctx, int lineno,
				 cfg_cable *cable)
{
	if (read_ratio(ctx, lineno, &cable->corrupt))
		return NULL;
	return cable;
}

static cfg_cable *add_duplication(parse_ctx_t *ctx, int lineno,
				  cfg_cable *cable)
{
	if (read_ratio(ctx, lineno, &cable->duplicate))
		return NULL;
	return cable;
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

static void netem_for_interface(cfg_cable *cable, const cfg_node_port *port,
				bandwidth_t bw)
{
	char temp[MAX_LIMIT_STR * 2], buffer[512];
	int have_netem = 0;

	sprintf(buffer, "tc qdisc add dev %s root netem", port->name);

	if (cable->delay) {
		sprintf(buffer + strlen(buffer), " delay %lums", cable->delay);
		have_netem = 1;
	}

	if (cable->loss > 0.0) {
		sprintf(buffer + strlen(buffer), " loss random %3.2f%%",
			cable->loss * 100.0);
		have_netem = 1;
	}

	if (cable->corrupt > 0.0) {
		sprintf(buffer + strlen(buffer), " corrupt %3.2f%%",
			cable->corrupt * 100.0);
		have_netem = 1;
	}

	if (cable->duplicate > 0.0) {
		sprintf(buffer + strlen(buffer), " duplicate %3.2f%%",
			cable->duplicate * 100.0);
		have_netem = 1;
	}

	if (bw.value) {
		cfg_bandwidth_to_str(temp, sizeof(temp), &bw);
		sprintf(buffer + strlen(buffer), " rate %s", temp);
		have_netem = 1;
	}

	if (have_netem)
		netns_run(port->owner->name, "%s", buffer);
}

static void configure_netem(cfg_cable *cable)
{
	netem_for_interface(cable, cable->upper, cable->uplimit);
	netem_for_interface(cable, cable->lower, cable->downlimit);
}

static int cable_drv_start(driver_t *drv)
{
	cfg_node_port *upper, *lower;
	cfg_cable *cable;
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

		/* reconfigure */
		node_configure_port(lower);

		configure_netem(cable);
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
		.flags = FLAG_ARG_MIN,
		.argcount = 2,
		.argfun = check_cable_port,
		.deserialize = (deserialize_fun_t)add_port,
	}, {
		.keyword = "loss",
		.flags = FLAG_ARG_EXACT,
		.argcount = 1,
		.argfun = check_ratio,
		.deserialize = (deserialize_fun_t)add_loss,
	}, {
		.keyword = "corrupt",
		.flags = FLAG_ARG_EXACT,
		.argcount = 1,
		.argfun = check_ratio,
		.deserialize = (deserialize_fun_t)add_corruption,
	}, {
		.keyword = "duplicate",
		.flags = FLAG_ARG_EXACT,
		.argcount = 1,
		.argfun = check_ratio,
		.deserialize = (deserialize_fun_t)add_duplication,
	}, {
		.keyword = "delay",
		.flags = FLAG_ARG_EXACT,
		.argcount = 1,
		.argfun = check_delay,
		.deserialize = (deserialize_fun_t)add_delay,
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
