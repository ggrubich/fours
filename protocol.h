#pragma once

#include <stddef.h>

enum message_type {
	MESSAGE_JOIN,
};

struct message {
	enum message_type type;
	union {
		struct {
			char *name;
		} join;
	} data;
};

void close_message(struct message *msg);

int parse_message(char *data, size_t len, struct message *msg);
