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
	struct buffer *buf;
	size_t idx;
	size_t len;
};

static int peek(struct parser *p)
{
	// ignore the trailing newline
	return p->idx >= p->len - 1 ? EOF : buffer_get(p->buf, p->idx);
}

static int next(struct parser *p)
{
	int c = peek(p);
	if (c == EOF) {
		return EOF;
	}
	p->idx += 1;
	return c;
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

static int format_integer(int x, struct buffer *buf)
{
	char bytes[16];
	sprintf(bytes, "%d", x);
	return buffer_push(buf, bytes, strlen(bytes));
}

static int format_symbol(char *sym, struct buffer *buf)
{
	return buffer_push(buf, sym, strlen(sym));
}

static int format_string(char *str, struct buffer *buf)
{
	size_t i, len;
	int res;
	len = strlen(str);
	if (buffer_push(buf, "\"", 1) < 0) {
		return -1;
	}
	for (i = 0; i < len; ++i) {
		switch (str[i]) {
		case '\\':
			res = buffer_push(buf, "\\\\", 2);
			break;
		case '\n':
			res = buffer_push(buf, "\\n", 2);
			break;
		case '"':
			res = buffer_push(buf, "\\\"", 2);
			break;
		default:
			res = buffer_push(buf, &str[i], 1);
		}
		if (res < 0) {
			return -1;
		}
	}
	return buffer_push(buf, "\"", 1);
}

static int format_raw_field(struct raw_field *field, struct buffer *buf)
{
	switch (field->type) {
	case FIELD_INTEGER:
		return format_integer(field->data.integer, buf);
	case FIELD_SYMBOL:
		return format_symbol(field->data.symbol, buf);
	case FIELD_STRING:
		return format_string(field->data.string, buf);
	default:
		return -1;
	}
}

static int format_raw_message(struct raw_message *msg, struct buffer *buf)
{
	size_t i;
	for (i = 0; i < msg->len; ++i) {
		if (format_raw_field(&msg->fields[i], buf) < 0) {
			return -1;
		}
		if (i + 1 < msg->len) {
			if (buffer_push(buf, " ", 1) < 0) {
				return -1;
			}
		}
	}
	return buffer_push(buf, "\n", 1);
}

void close_message(struct message *msg)
{
	switch (msg->type) {
	case MSG_JOIN:
		free(msg->data.join.name);
		break;
	default:
		break;
	}
}

static size_t message_length(struct buffer *buf)
{
	int i;
	for (i = 0; i < buffer_len(buf); ++i) {
		if (buffer_get(buf, i) == '\n') {
			return i + 1;
		}
	}
	return 0;
}

static int decode_message(struct raw_message *raw, struct message *msg)
{
	if (raw->len == 0 || raw->fields[0].type != FIELD_SYMBOL) {
		return -1;
	}
	if (strcmp(raw->fields[0].data.symbol, "invalid") == 0) {
		msg->type = MSG_INVALID;
	} else if (strcmp(raw->fields[0].data.symbol, "join") == 0) {
		if (raw->len != 2 || raw->fields[1].type != FIELD_STRING) {
			return -1;
		}
		msg->type = MSG_JOIN;
		msg->data.join.name = raw->fields[1].data.string;
		raw->fields[1].data.string = NULL;
	} else {
		return -1;
	}
	return 0;
}

int parse_message(struct buffer *buf, struct message *msg)
{
	struct parser parser;
	struct raw_message raw;
	int len = message_length(buf);
	if (len == 0) {
		return 0;
	}
	parser.buf = buf;
	parser.idx = 0;
	parser.len = len;
	if (parse_raw_message(&parser, &raw) < 0) {
		return -(int)len;
	}
	if (decode_message(&raw, msg) < 0) {
		close_raw_message(&raw);
		return -(int)len;
	}
	close_raw_message(&raw);
	return len;
}

static int init_raw_message(struct raw_message *raw, size_t len)
{
	size_t i;
	raw->fields = malloc(len * sizeof(*raw->fields));
	if (!raw->fields) {
		return -1;
	}
	raw->len = raw->cap = len;
	for (i = 0; i < len; ++i) {
		raw->fields[i].type = FIELD_INTEGER;
		raw->fields[i].data.integer = 0;
	}
	return 0;
}

static int encode_message(struct message *msg, struct raw_message *raw)
{
	switch (msg->type) {
	case MSG_INVALID:
		if (init_raw_message(raw, 1) < 0) {
			return -1;
		}
		raw->fields[0].type = FIELD_SYMBOL;
		raw->fields[0].data.symbol = "invalid";
		break;
	case MSG_JOIN:
		if (init_raw_message(raw, 2) < 0) {
			return -1;
		}
		raw->fields[0].type = FIELD_SYMBOL;
		raw->fields[0].data.symbol = "join";
		raw->fields[1].type = FIELD_STRING;
		raw->fields[1].data.string = msg->data.join.name;
		break;
	default:
		return -1;
	}
	return 0;
}

int format_message(struct message *msg, struct buffer *buf)
{
	struct raw_message raw;
	int res;
	if (encode_message(msg, &raw) < 0) {
		return -1;
	}
	res = format_raw_message(&raw, buf);
	free(raw.fields);
	return res;
}
