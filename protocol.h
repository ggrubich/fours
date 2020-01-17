#pragma once

#include <stddef.h>

#include "buffer.h"

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

/* Deallocates memory associated with the message.
 */
void close_message(struct message *msg);

/* Reads the next message in the buffer. This function will not mutate
 * the buffer.
 *
 * On success returns the length (in bytes) of the message that was read
 * and on failure it returns the negated length of the message that it tried
 * to parse. The length includes message's trailing newline.
 * If the buffer doesn't contain any complete messages, function will return 0.
 */
int parse_message(struct buffer *buf, struct message *msg);
