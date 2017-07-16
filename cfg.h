#ifndef CFG_H
#define CFG_H


#include <sys/socket.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


#define TEMPDIR "/tmp"
#define TEMPNAME "nettool"

#define MIN_NAME 1
#define MAX_NAME 32
#define MAX_CLEANUP_HANDLERS 32

#define ADDR_TYPE_V4 1
#define ADDR_TYPE_V6 2


typedef struct {
	unsigned int value;
	unsigned long scale;
} bandwidth_t;


typedef enum {
	TK_END = 0,
	TK_BLOCK,
	TK_ARG,
	TK_USER,
} TOKEN_ID;

enum {
	FLAG_ARG_MIN = 0x01,	/* argcount is minimum, more are allowed */
	FLAG_ARG_MAX = 0x02,	/* argcount is maximum, fewer are allowed */
	FLAG_ARG_EXACT = FLAG_ARG_MIN|FLAG_ARG_MAX,
};

typedef struct {
	uint8_t id;
	uint16_t linenumber;
} __attribute__((__packed__)) cfg_token_t;

typedef struct {
	int fd;
	off_t readoff;
} parse_ctx_t;

typedef int (*argument_fun_t)(parse_ctx_t *ctx, int index, int lineno);
typedef void* (*deserialize_fun_t)(parse_ctx_t *ctx, int lineno, void *parent);
typedef void (*cleanup_fun_t)(void);

typedef struct parser_token_t {
	const char *keyword;
	int token;
	int flags;
	size_t argcount;
	argument_fun_t argfun;
	deserialize_fun_t deserialize;
	struct parser_token_t *children;
} parser_token_t;



#define EXPORT_PARSER(tk) \
	static void __attribute__((constructor)) register_##tk(void) { \
		cfg_register_parser_token(&tk); \
	}

#define EXPORT_CLEANUP_HANDLER(handler) \
	static void __attribute__((constructor)) regiser_##handler(void) {\
		cfg_register_cleanup_handler(handler);\
	}

/*
	Read the next token from an input file (represented by parser
	context). If peek is non-zero, don't advance the file pointer,
	only look at the next token.

	Returns: A positive value on success, a negative value on failure
		or zero if there are no further tokens.
 */
int cfg_next_token(parse_ctx_t *ctx, cfg_token_t *tk, int peek);

/*
	Read a null-terminated string into a buffer. If buffer is NULL,
	simply skip past the string without reading it.

	Returns: Negative value on failure, zero on success.
 */
int cfg_get_arg(parse_ctx_t *ctx, char *buffer, size_t size);

/* Read a sequence of argument tokens into an argument vector */
char **cfg_read_argvec(parse_ctx_t *ctx, int *argc, int lineno);

/* Print a text describing a token ID to a file. */
void cfg_print_token(FILE *f, int tk);

/*
	Called by cfg_read to generated tokens from a text based configuration
	file. Whitespace and comments are removed. A cfg_token_t is emitted
	for each successfully processed token. A TK_ARG token is followed by
	a null-terminated string.

	Returns: Negative value on failure, zero on success.
 */
int cfg_tokenize(int infd, int outfd);

/*
	Called by cfg_read on a file descriptor pointing to the intermediate
	file generated by the tokenization step. Analyzes the structure of
	the file for adherence to the configuration grammar rules.

	Returns: Negative value on failure, zero on success.
 */
int cfg_parse(int fd);

/*
	Process a configuration file. The file is tokenized and parsed, the
	resulting token soup stored in an intermediate temporary file. The
	result is then deserialized into a global configuration state.

	Returns: Negative value on failure, zero on success.
 */
int cfg_read(const char *file);

/* Reset the internal state generated by cfg_read() */
void cfg_cleanup(void);

/*
	Register a keyword for the tokenizer. A unique ID is generated for the
	keyword. If a keyword is registered a second time, the same ID is
	returned without adding a new entry.

	Returns: A negative value on failure, a positive unique ID on success.
 */
int cfg_register_keyword(const char *name);

/*
	Add a hierarchical structure of configuration data to the parser. The
	registered object hierarchy is used for checking syntax and for
	deserializing the configuration data. The keywords in the hierarchy
	are automatically registered (storing the IDs in place) an the supplied
	pointers are used internally, so they have to stick around until
	cfg_unregister_parser_tokens() is called.

	Returns: zero on success, a negative value on failure.
 */
int cfg_register_parser_token(parser_token_t *tk);

/* Flush the keywords stored in the tokenizer */
void cfg_unregister_keywords(void);

/* Flush all registered syntax structures from the parser */
void cfg_unregister_parser_tokens(void);

/*
	Check if a name string is valid, i.e. between MIN_NAME and MAX_NAME,
	only contains alphanumeric characters or '_' and begins with a letter.

	The supplied line number is used for printing a fancy error message.
 */
int cfg_check_name(const char *name, int lineno);

/*
	Read an argument string and check if it is a valid name. The supplied
	index is ignored and only accepted, so it can be used as an argfun
	for parser_token_t.
 */
int cfg_check_name_arg(parse_ctx_t *ctx, int index, int lineno);

/*
	Parse an IPv4 or IPv6 address and optional subnet mask from a
	string (buffer is modified). The supplied lineno is used for
	printing fancy error messages.

	If the out, len and netmask pointers are not NULL, they will
	be set to the resulting address, address length and subnet
	mask respectively.
 */
int cfg_parse_ip_addr(char *buffer, int lineno,
			struct sockaddr_storage *out, socklen_t *len,
			int *netmask);

/*
	Read an argument string and run it through cfg_parse_ip_addr. The
	supplied index is ignored and only accepted, so it can be used as
	an argfun for parser_token_t.
 */
int cfg_check_ip_addr_arg(parse_ctx_t *ctx, int index, int lineno);

int cfg_parse_bandwidth(const char *buffer, int lineno, bandwidth_t *bw);

int cfg_bandwidth_to_str(char *buffer, size_t len, bandwidth_t *bw);

/*
	Register a function that is called by cfg_cleanup when flushing
	out the current configuration.
 */
int cfg_register_cleanup_handler(cleanup_fun_t fun);

#endif /* CFG_H */

