#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include <arpa/inet.h>

#include "cfg.h"

static cleanup_fun_t cleanup_handlers[MAX_CLEANUP_HANDLERS];
static size_t num_clenaup_handlers = 0;



static struct {
	const char *str;
	unsigned long scale;
} suffix[] = {
	{ "bit",   1UL },
	{ "Kibit", 1024UL },
	{ "kbit",  1000UL },
	{ "mibit", 1024UL * 1024UL },
	{ "mbit",  1000UL * 1000UL },
	{ "gibit", 1024UL * 1024UL * 1024UL },
	{ "gbit",  1000UL * 1000UL * 1000UL },
	{ "tibit", 1024UL * 1024UL * 1024UL * 1024UL },
	{ "tbit",  1000UL * 1000UL * 1000UL * 1000UL },
	{ "Bps",   8UL },
	{ "KiBps", 8UL * 1024UL },
	{ "KBps",  8UL * 1000UL },
	{ "MiBps", 8UL * 1024UL * 1024UL },
	{ "MBps",  8UL * 1000UL * 1000UL },
	{ "GiBps", 8UL * 1024UL * 1024UL * 1024UL },
	{ "GBps",  8UL * 1000UL * 1000UL * 1000UL },
	{ "TiBps", 8UL * 1024UL * 1024UL * 1024UL * 1024UL },
	{ "TBps",  8UL * 1000UL * 1000UL * 1000UL * 1000UL },
	{ NULL, 0 },
};


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

int cfg_parse_bandwidth(const char *buffer, int lineno, bandwidth_t *bw)
{
	unsigned int value = 0;
	const char *ptr;
	size_t i;

	memset(bw, 0, sizeof(*bw));

	if (!buffer[0])
		return 0;

	if (!isdigit(buffer[0])) {
		fprintf(stderr, "%d: bandwidth must be "
			"integer value\n", lineno);
		return -1;
	}

	for (ptr = buffer; isdigit(*ptr); ++ptr)
		value = value * 10 + (*ptr - '0');

	if (bw) {
		bw->value = value;
		bw->scale = 1;
	}

	if (*ptr) {
		for (i = 0; suffix[i].str; ++i) {
			if (!strcasecmp(ptr, suffix[i].str))
				break;
		}
		if (!suffix[i].str) {
			fprintf(stderr, "%d: unknown suffix '%s'\n",
				lineno, ptr);
			return -1;
		}

		if (bw)
			bw->scale = suffix[i].scale;
	}

	return 0;
}

int cfg_parse_ratio(const char *buffer, int lineno, double *out)
{
	unsigned int a = 0, b = 0, blen = 0;
	const char *ptr = buffer;

	*out = 0.0;

	/* digit+ */
	if (!isdigit(*ptr))
		goto fail;

	for (ptr = buffer; isdigit(*ptr); ++ptr)
		a = a * 10 + (*ptr - '0');

	*out = a;

	/* ['.' digit+] */
	if (*ptr == '.') {
		++ptr;

		if (!isdigit(*ptr))
			goto fail;

		for (; isdigit(*ptr); ++ptr) {
			b = b * 10 + (*ptr - '0');
			++blen;
		}

		*out += b * pow(10.0, -((double)blen));
	}

	/* ['%'] */
	if (*ptr == '%') {
		++ptr;
		*out *= 0.01;
	}

	if (*ptr)
		goto fail;

	if (*out > 1.0) {
		fprintf(stderr, "%d: ratio larger than 100%%!\n", lineno);
		return -1;
	}

	return 0;
fail:
	fprintf(stderr, "%d: expected numeric value\n", lineno);
	return -1;
}

int cfg_bandwidth_to_str(char *buffer, size_t len, bandwidth_t *bw)
{
	size_t i, sfx = 0;
	double ratio;
	int ret;

	for (i = 0; suffix[i].str; ++i) {
		if (suffix[i].scale > bw->scale)
			continue;
		if (suffix[i].scale <= suffix[sfx].scale)
			continue;
		sfx = i;
	}

	if (suffix[sfx].scale == bw->scale) {
		ret = snprintf(buffer, len, "%u%s",
			       bw->value,suffix[sfx].str);
	} else {
		ratio = (double)bw->scale / (double)suffix[sfx].scale;

		ret = snprintf(buffer, len, "%3.3f%s",
			       bw->value * ratio, suffix[sfx].str);
	}

	if (ret < 0 || ((size_t)ret >= len))
		return -1;

	return 0;
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

