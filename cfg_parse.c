#include "cfg.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>


static parser_token_t* global_tokens = NULL;
static size_t max_global_tokens = 0;
static size_t num_global_tokens = 0;


static int proccess_arguments(parse_ctx_t *ctx, const parser_token_t *tk,
				int lineno)
{
	cfg_token_t next;
	size_t argcount;
	int ret;

	for (argcount = 0; ; ++argcount) {
		ret = cfg_next_token(ctx, &next, 1);
		if (ret < 0)
			return -1;
		if (ret == 0 || next.id != TK_ARG)
			break;
		ctx->readoff += sizeof(next);

		if (tk->argfun) {
			ret = tk->argfun(ctx, argcount, next.linenumber);
		} else {
			ret = cfg_get_arg(ctx, NULL, 0);
		}

		if (ret)
			return -1;
	}

	if ((tk->flags & FLAG_ARG_MIN) && (argcount < tk->argcount)) {
		fprintf(stderr, "%d: too few arguments for ", lineno);
		cfg_print_token(stderr, tk->token);
		fputc('\n', stderr);
		return -1;
	}

	if ((tk->flags & FLAG_ARG_MAX) && (argcount > tk->argcount)) {
		fprintf(stderr, "%d: too many arguments for ", lineno);
		cfg_print_token(stderr, tk->token);
		fputc('\n', stderr);
		return -1;
	}
	return 0;
}

static int process_token(parse_ctx_t *ctx, const parser_token_t *allowed,
			void *parent)
{
	cfg_token_t tk, next;
	void *object;
	off_t offset;
	size_t i;
	int ret;

	/* consume next token */
	ret = cfg_next_token(ctx, &tk, 0);

	if (ret <= 0)
		return ret;

	for (i = 0; allowed[i].token; ++i) {
		if (tk.id == allowed[i].token)
			break;
	}

	if (!allowed[i].token)
		goto fail_kw;

	/* check arguments */
	offset = ctx->readoff;

	if (proccess_arguments(ctx, allowed + i, tk.linenumber))
		return -1;

	/* deserialize */
	ctx->readoff = offset;

	object = allowed[i].deserialize(ctx, tk.linenumber, parent);
	if (!object)
		return -1;

	/* recursively handle children */
	if (allowed[i].children) {
		ret = cfg_next_token(ctx, &next, 0);
		if (ret < 0)
			return -1;
		if (ret == 0 || next.id != TK_BLOCK)
			goto fail_block;

		while (1) {
			ret = cfg_next_token(ctx, &next, 1);
			if (ret < 0)
				return -1;
			if (ret == 0)
				goto fail_end;

			if (next.id == TK_END) {
				ctx->readoff += sizeof(next);
				break;
			}

			ret = process_token(ctx, allowed[i].children, object);
			if (ret < 0)
				return -1;
			if (ret == 0)
				goto fail_end;
		}
	}
	return 1;
fail_kw:
	fprintf(stderr, "%d: Unexpected ", tk.linenumber);
	cfg_print_token(stderr, tk.id);
	fputc('\n', stderr);
	return -1;
fail_block:
	fprintf(stderr, "%d: expected '{' after ", next.linenumber);
	cfg_print_token(stderr, allowed[i].token);
	fputc('\n', stderr);
	return -1;
fail_end:
	fputs("missing '}' before end of file\n", stderr);
	return -1;
}

int cfg_parse(int fd)
{
	parse_ctx_t ctx = { .fd = fd, .readoff = 0 };
	int ret;

	do {
		ret = process_token(&ctx, global_tokens, NULL);
	} while (ret > 0);

	return ret;
}

static void register_keywords(parser_token_t *tk)
{
	size_t i;

	tk->token = cfg_register_keyword(tk->keyword);

	if (tk->children) {
		for (i = 0; tk->children[i].keyword != NULL; ++i) {
			register_keywords(tk->children + i);
		}
	}
}

int cfg_register_parser_token(parser_token_t *tk)
{
	size_t new_count;
	void *new;

	if (max_global_tokens == num_global_tokens) {
		new_count = max_global_tokens ? max_global_tokens * 2 : 10;

		new = realloc(global_tokens,
				new_count * sizeof(*global_tokens));

		if (!new) {
			fprintf(stderr, "[cfg_register_parser_token] out"
					" of memory\n");
			return -1;
		}

		global_tokens = new;
		max_global_tokens = new_count;
	}

	register_keywords(tk);

	global_tokens[num_global_tokens++] = *tk;
	memset(global_tokens + num_global_tokens, 0, sizeof(*global_tokens));
	return 0;
}

void cfg_unregister_parser_tokens(void)
{
	free(global_tokens);
	num_global_tokens = max_global_tokens = 0;
}

