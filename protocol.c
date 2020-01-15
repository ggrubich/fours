#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "protocol.h"

enum field_type {
	FIELD_INTEGER,
	FIELD_SYMBOL,
	FIELD_STRING,
};

struct raw_field {
	enum field_type type;
	union {
		int integer;
		char *symbol;
		char *string;
	} data;
};

struct raw_message {
	struct raw_field *fields;
	size_t len;
	size_t cap;
};

static void close_raw_field(struct raw_field *field)
{
	switch (field->type) {
	case FIELD_SYMBOL:
		free(field->data.symbol);
		break;
	case FIELD_STRING:
		free(field->data.string);
		break;
	default:
		return;
	}
}

static void close_raw_message(struct raw_message *msg)
{
	size_t i;
	for (i = 0; i < msg->len; ++i) {
		close_raw_field(&msg->fields[i]);
	}
	free(msg->fields);
}

struct parser {
	char *cur;
	char *end;
};

static int next(struct parser *p)
{
	int c;
	if (p->cur >= p->end) {
		return EOF;
	}
	c = *p->cur;
	++p->cur;
	return c;
}

static int peek(struct parser *p)
{
	return p->cur >= p->end ? EOF : *p->cur;
}

static int parse_integer(struct parser *p, int *out)
{
	int c;
	int sign = 1;
	unsigned val;
	if (peek(p) == '-') {
		sign = -1;
		next(p);
	} else if (peek(p) == '+') {
		sign = 1;
		next(p);
	}
	if (!isdigit(peek(p))) {
		return -1;
	}
	c = next(p);
	val = c - '0';
	while (isdigit(peek(p))) {
		c = next(p);
		val = (10 * val) + (c - '0');
	}
	*out = sign * (int)val;
	return 0;
}

const size_t MAX_SYMBOL_LEN = 32;

static int parse_symbol(struct parser *p, char **out)
{
	size_t len = 0;
	if (!isalpha(peek(p))) {
		return -1;
	}
	*out = malloc(MAX_SYMBOL_LEN + 1);
	if (!*out) {
		return -1;
	}
	(*out)[len++] = next(p);
	while (len < MAX_SYMBOL_LEN && (isalnum(peek(p)) || strchr("-_", peek(p)))) {
		(*out)[len++] = next(p);
	}
	(*out)[len] = '\0';
	return 0;
}

static int parse_string(struct parser *p, char **out)
{
	char *tmp;
	char c;
	size_t len = 0;
	size_t size = 32;
	*out = malloc(size);
	if (!*out) {
		return -1;
	}
	if (peek(p) != '"') {
		free(*out);
		return -1;
	}
	next(p);
	while (peek(p) != EOF && peek(p) != '"') {
		c = next(p);
		if (len + 1 == size) {
			tmp = realloc(*out, 2 * size);
			if (!tmp) {
				free(*out);
				return -1;
			}
			*out = tmp;
			size = 2 * size;
		}
		if (c == '\\') {
			switch (next(p)) {
			case '\\':
				c = '\\';
				break;
			case 'n':
				c = '\n';
				break;
			case '"':
				c = '"';
				break;
			default:
				free(*out);
				return -1;
			}
		}
		(*out)[len++] = c;
	}
	if (peek(p) != '"') {
		free(*out);
		return -1;
	}
	next(p);
	(*out)[len] = '\0';
	return 0;
}

static int parse_raw_field(struct parser *p, struct raw_field *field)
{
	struct parser save = *p;
	if (parse_integer(p, &field->data.integer) == 0) {
		field->type = FIELD_INTEGER;
		return 0;
	}
	*p = save;
	if (parse_symbol(p, &field->data.symbol) == 0) {
		field->type = FIELD_SYMBOL;
		return 0;
	}
	*p = save;
	if (parse_string(p, &field->data.string) == 0) {
		field->type = FIELD_STRING;
		return 0;
	}
	return -1;
}

static int parse_raw_message(struct parser *p, struct raw_message *msg)
{
	struct raw_field *tmp;
	msg->len = 0;
	msg->cap = 2;
	msg->fields = malloc(msg->cap * sizeof(*msg->fields));
	if (!msg->fields) {
		return -1;
	}
	while (isspace(peek(p))) {
		next(p);
	}
	while (peek(p) != EOF) {
		if (msg->len == msg->cap) {
			tmp = realloc(msg->fields, 2 * msg->cap * sizeof(*msg->fields));
			if (!tmp) {
				free(msg->fields);
				return -1;
			}
			msg->fields = tmp;
			msg->cap = 2 * msg->cap;
		}
		if (parse_raw_field(p, &msg->fields[msg->len]) < 0) {
			close_raw_message(msg);
			return -1;
		}
		++msg->len;
		while (isspace(peek(p))) {
			next(p);
		}
	}
	return 0;
}

void close_message(struct message *msg)
{
	switch (msg->type) {
	case MESSAGE_JOIN:
		free(msg->data.join.name);
		break;
	}
}

int parse_message(char *data, size_t len, struct message *msg)
{
	struct parser parser = {data, data + len};
	struct raw_message raw;
	if (parse_raw_message(&parser, &raw) < 0) {
		return -1;
	}
	if (raw.len == 0 || raw.fields[0].type != FIELD_SYMBOL) {
		close_raw_message(&raw);
		return -1;
	}
	if (strcmp(raw.fields[0].data.symbol, "join") == 0) {
		if (raw.len != 2 || raw.fields[1].type != FIELD_STRING) {
			close_raw_message(&raw);
			return -1;
		}
		msg->type = MESSAGE_JOIN;
		msg->data.join.name = raw.fields[1].data.string;
		raw.fields[1].data.string = NULL;
	} else {
		close_raw_message(&raw);
		return -1;
	}
	close_raw_message(&raw);
	return 0;
}
