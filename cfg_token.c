#include "cfg.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>


typedef struct cfg_keyword_t {
	char *name;
	int token;
	struct cfg_keyword_t *next;
} cfg_keyword_t;


static cfg_keyword_t *keywords;
static int next_token = TK_USER + 1;


static int skip_line(int infd, int lineno)
{
	int ret;
	char c;

	do {
		ret = read(infd, &c, 1);
		if (ret < 0) {
			fprintf(stderr, "%d: reading from input file: %s\n",
				lineno, strerror(errno));
			return -1;
		}
	} while (ret && c != '\n');

	return 0;
}

static int write_token(int fd, int lineno, int id)
{
	cfg_token_t token = { .id = id, .linenumber = lineno };
	int ret;

	ret = write(fd, &token, sizeof(token));

	if (ret < 0) {
		perror("writing to temporary file");
		return -1;
	}

	if ((size_t)ret < sizeof(token)) {
		fputs("writing to temporary file: short write\n", stderr);
		return -1;
	}

	return 0;
}

static int handle_keyword(int outfd, const char *name, int lineno)
{
	cfg_keyword_t *kw;

	for (kw = keywords; kw != NULL; kw = kw->next) {
		if (!strcmp(name, kw->name)) {
			write_token(outfd, lineno, kw->token);
			return 0;
		}
	}

	fprintf(stderr, "%d: unknown keyword '%s'\n", lineno, name);
	return -1;
}

static int copy_string(int infd, int outfd, int lineno)
{
	int ret;
	char c;

	write_token(outfd, lineno, TK_ARG);

	do {
		ret = read(infd, &c, 1);
		if (ret < 0)
			goto fail_errno;
		if (ret == 0)
			goto fail_str_lf;

		if (c == '\\') {
			ret = read(infd, &c, 1);
			if (ret < 0)
				goto fail_errno;
			if (ret == 0)
				goto fail_str_lf;

			switch (c) {
			case '"':
			case '\\':
				break;
			default:
				goto fail_esc;
			}
		} else if (strchr("\r\n\v\f", c)) {
			goto fail_str_lf;
		} else if (c == '"') {
			c = '\0';
		}

		write(outfd, &c, 1);
	} while (c != '\0');

	return 0;
fail_errno:
	fprintf(stderr, "%d: reading from input file: %s\n",
		lineno, strerror(errno));
	return -1;
fail_str_lf:
	fprintf(stderr, "%d: missing '\"' before end of line\n", lineno);
	return -1;
fail_esc:
	if (isgraph(c)) {
		fprintf(stderr, "%d: unknown escape sequence \\%c\n",
			lineno, c);
	} else {
		fprintf(stderr, "%d: unexpected '\\u%04X'\n", lineno, (int)c);
	}
	return -1;
}

void cfg_print_token(FILE *f, int tk)
{
	cfg_keyword_t *kw;

	for (kw = keywords; kw != NULL; kw = kw->next) {
		if (kw->token == tk) {
			fprintf(f, "'%s'", kw->name);
			return;
		}
	}

	switch (tk) {
	case TK_END: fputs("'}'", f); break;
	case TK_BLOCK: fputs("'{'", f); break;
	case TK_ARG: fputs("argument", f); break;
	}
}

int cfg_tokenize(int infd, int outfd)
{
	int ret, lineno = 1, kw_arg = 0;
	char d, c, buffer[64];
	size_t i;

	while (1) {
		ret = read(infd, &c, 1);
		if (ret < 0)
			goto fail_errno;
		if (ret == 0)
			break;
	next:
		switch (c) {
		case '\t':
		case ' ':
			continue;
		case '#':
			if (skip_line(infd, lineno))
				goto fail_errno;
			/* XXX: fallthrough */
		case '\n':
			++lineno;
			kw_arg = 0;
			continue;
		case '{':
			write_token(outfd, lineno, TK_BLOCK);
			kw_arg = 0;
			continue;
		case '}':
			write_token(outfd, lineno, TK_END);
			kw_arg = 0;
			continue;
		case '"':
			if (!kw_arg)
				goto fail_str;
			if (copy_string(infd, outfd, lineno))
				return -1;
			continue;
		}

		if (kw_arg) {
			write_token(outfd, lineno, TK_ARG);
			write(outfd, &c, 1);
			do {
				ret = read(infd, &c, 1);
				if (ret < 0)
					goto fail_errno;
				if (!ret || strchr("#{}", c) || isspace(c)) {
					d = '\0';
				} else {
					d = c;
				}
				write(outfd, &d, 1);
			} while (d != '\0');
			if (ret == 0)
				break;
			goto next; /* XXX: we already have the next char */
		}

		if (!isalpha(c))
			goto fail_token;

		buffer[0] = c;
		for (i = 1; i < (sizeof(buffer) - 1); ++i) {
			ret = read(infd, &c, 1);
			if (ret < 0)
				goto fail_errno;
			if (!ret || (!isalnum(c) && c != '_'))
				break;
			buffer[i] = c;
		}
		buffer[i] = '\0';

		if (handle_keyword(outfd, buffer, lineno))
			return -1;
		if (ret == 0)
			break;
		kw_arg = 1;
		goto next;	/* XXX: we already have the next char */
	}
	return 0;
fail_errno:
	fprintf(stderr, "%d: reading from input file: %s\n",
		lineno, strerror(errno));
	return -1;
fail_token:
	if (isgraph(c)) {
		fprintf(stderr, "%d: unexpected '%c'\n", lineno, c);
	} else {
		fprintf(stderr, "%d: unexpected '\\u%04X'\n", lineno, (int)c);
	}
	return -1;
fail_str:
	fprintf(stderr, "%d: expected keyword before string\n", lineno);
	return -1;
}

int cfg_register_keyword(const char *name)
{
	cfg_keyword_t *kw;

	for (kw = keywords; kw != NULL; kw = kw->next) {
		if (!strcmp(kw->name, name))
			return kw->token;
	}

	kw = calloc(1, sizeof(*kw));
	if (!kw)
		goto fail_alloc;

	kw->name = strdup(name);
	if (!kw->name) {
		free(kw);
		goto fail_alloc;
	}

	kw->token = next_token++;
	kw->next = keywords;
	keywords = kw;
	return kw->token;
fail_alloc:
	fprintf(stderr, "[cfg_register_keyword] out of memory\n");
	return -1;
}

void cfg_unregister_keywords(void)
{
	cfg_keyword_t *kw;

	while (keywords != NULL) {
		kw = keywords;
		keywords = keywords->next;

		free(kw->name);
		free(kw);
	}
}

