#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>

#include <arpa/inet.h>

#include "cfg.h"

static cleanup_fun_t cleanup_handlers[MAX_CLEANUP_HANDLERS];
static size_t num_clenaup_handlers = 0;

int cfg_get_arg(parse_ctx_t *ctx, char *buffer, size_t size)
{
	size_t i = 0;
	int ret;
	char c;

	do {
		ret = pread(ctx->fd, &c, 1, ctx->readoff++);
		if (ret < 0) {
			perror("read on temporary file");
			return -1;
		}
		if (ret == 0) {
			fputs("[BUG] end of file while reading argument",
				stderr);
			return -1;
		}

		if (buffer) {
			if (i == (size - 1))
				c = '\0';

			buffer[i++] = c;
		}
	} while (c != '\0');

	return 0;
}

int cfg_check_name(const char *name, int lineno)
{
	int i, len;

	len = strlen(name);

	for (i = 0; i < len; ++i) {
		if (!isalnum(name[i]) && name[i] != '_') {
			fprintf(stderr, "%d: name '%s' contains invalid "
				"chararacters (not alphanumeric or '_')\n",
				lineno, name);
			return -1;
		}
	}

	if (len < MIN_NAME) {
		fprintf(stderr, "%d: name '%s' is too short (minimum is %d "
			"chararacters)\n", lineno, name, MIN_NAME);
		return -1;
	}

	if (len > MAX_NAME) {
		fprintf(stderr, "%d: name '%s' is too long (maximum is %d "
			"chararacters)\n", lineno, name, MAX_NAME);
		return -1;
	}

	if (!isalpha(name[0])) {
		fprintf(stderr, "%d: name '%s' must begin with a letter\n",
			lineno, name);
		return -1;
	}
	return 0;
}

int cfg_check_name_arg(parse_ctx_t *ctx, int index, int lineno)
{
	char buffer[MAX_NAME * 2];
	(void)index;

	if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
		return -1;

	return cfg_check_name(buffer, lineno);
}

int cfg_parse_ip_addr(char *buffer, int lineno,
			struct sockaddr_storage *out, socklen_t *len,
			int *netmask)
{
	struct in6_addr addr6;
	struct in_addr addr4;
	int mask, type;
	char *submask;
	size_t i = 0;

	submask = strrchr(buffer, '/');
	if (submask)
		*(submask++) = '\0';

	if (inet_pton(AF_INET, buffer, &addr4) > 0) {
		type = ADDR_TYPE_V4;
		if (len)
			*len = sizeof(struct sockaddr_in);
		if (out) {
			memset(out, 0, sizeof(*out));
			out->ss_family = AF_INET;
			((struct sockaddr_in *)out)->sin_addr = addr4;
		}
	} else if (inet_pton(AF_INET6, buffer, &addr6) > 0) {
		type = ADDR_TYPE_V6;
		if (len)
			*len = sizeof(struct sockaddr_in6);
		if (out) {
			memset(out, 0, sizeof(*out));
			out->ss_family = AF_INET6;
			((struct sockaddr_in6 *)out)->sin6_addr = addr6;
		}
	} else {
		goto fail;
	}

	mask = 0;

	if (submask) {
		if (!isdigit(*submask))
			goto fail_submask;
		for (i = 0; isdigit(submask[i]); ++i)
			mask = mask * 10 + submask[i] - '0';
		if (submask[i])
			goto fail_submask;
	} else {
		if (type == ADDR_TYPE_V4)
			mask = 32;
		if (type == ADDR_TYPE_V6)
			mask = 128;
	}

	if (type == ADDR_TYPE_V4 && mask > 32)
		goto fail_mask_range;
	if (type == ADDR_TYPE_V6 && mask > 128)
		goto fail_mask_range;

	if (netmask)
		*netmask = mask;
	return 0;
fail:
	fprintf(stderr, "%d: expected IPv4 or IPv6 address, found '%s'\n",
		lineno, buffer);
	return -1;
fail_submask:
	fprintf(stderr, "%d: expected subnetmask after '%s', found '%s'\n",
		lineno, buffer, submask);
	return -1;
fail_mask_range:
	fprintf(stderr, "%d: subnetmask %d out of range for '%s'\n",
		lineno, mask, buffer);
	return -1;
}

int cfg_check_ip_addr_arg(parse_ctx_t *ctx, int index, int lineno)
{
	char buffer[64];
	(void)index;

	if (cfg_get_arg(ctx, buffer, sizeof(buffer)))
		return -1;

	return cfg_parse_ip_addr(buffer, lineno, NULL, NULL, NULL);
}

int cfg_next_token(parse_ctx_t *ctx, cfg_token_t *tk, int peek)
{
	int ret = pread(ctx->fd, tk, sizeof(*tk), ctx->readoff);

	if (ret < 0)
		perror("read on temporary file");
	if (ret <= 0)
		return ret;
	if ((size_t)ret < sizeof(*tk)) {
		fputs("short read on temporary file\n", stderr);
		return -1;
	}

	if (!peek)
		ctx->readoff += ret;
	return ret;
}

char **cfg_read_argvec(parse_ctx_t *ctx, int *argc, int lineno)
{
	int i, count = 0, max = 0, len;
	char **argv = NULL;
	cfg_token_t tk;
	void *new;
	off_t old;

	while (1) {
		if (cfg_next_token(ctx, &tk, 1) < 0)
			goto fail_free;
		if (tk.id != TK_ARG)
			break;
		ctx->readoff += sizeof(tk);

		/* measure length */
		old = ctx->readoff;
		if (cfg_get_arg(ctx, NULL, 0))
			goto fail_free;
		len = ctx->readoff - old + 1;
		ctx->readoff = old;

		/* alloc space */
		if (count == max) {
			max = max ? max * 2 : 10;
			new = realloc(argv, max * sizeof(char*));
			if (!new)
				goto fail_alloc;
			argv = new;
		}

		argv[count] = calloc(1, len);
		if (!argv[count])
			goto fail_alloc;

		/* read */
		assert(cfg_get_arg(ctx, argv[count++], len) == 0);
	}

	*argc = count;
	return argv;
fail_alloc:
	fprintf(stderr, "%d: out of memory\n", lineno);
fail_free:
	for (i = 0; i < count; ++i) {
		free(argv[i]);
	}
	free(argv);
	return NULL;
}

int cfg_read(const char *file)
{
	char tmp_file_name[64];
	int fd, tempfd;

	/* open input file */
	fd = open(file, O_RDONLY);
	if (fd < 0) {
		perror(file);
		return -1;
	}

	/* open temporary output file */
	sprintf(tmp_file_name, "%s/%sXXXXXX", TEMPDIR, TEMPNAME);
	tempfd = mkstemp(tmp_file_name);

	if (tempfd < 0) {
		perror(tmp_file_name);
		goto fail_infd;
	}

	/* process file */
	if (cfg_tokenize(fd, tempfd))
		goto fail;
	if (cfg_parse(tempfd))
		goto fail;

	close(fd);
	close(tempfd);

	if (unlink(tmp_file_name) != 0)
		perror(tmp_file_name);
	return 0;
fail:
	close(tempfd);
	if (unlink(tmp_file_name) != 0)
		perror(tmp_file_name);
fail_infd:
	close(fd);
	return -1;
}

void cfg_cleanup(void)
{
	size_t i;

	for (i = 0; i < num_clenaup_handlers; ++i)
		cleanup_handlers[i]();
}

int cfg_register_cleanup_handler(cleanup_fun_t fun)
{
	assert(num_clenaup_handlers < MAX_CLEANUP_HANDLERS);
	cleanup_handlers[num_clenaup_handlers++] = fun;
	return 0;
}

