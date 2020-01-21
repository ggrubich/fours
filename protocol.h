#pragma once

#include <stddef.h>

#include "buffer.h"

enum message_type {
	MSG_INVALID,
	MSG_LOGIN,
	MSG_LOGIN_OK,
	MSG_LOGIN_ERR,
	MSG_START,
	MSG_START_OK,
	MSG_START_ERR,
	MSG_DROP,
	MSG_DROP_OK,
	MSG_DROP_ERR,
	MSG_NOTIFY_DROP,
	MSG_NOTIFY_OVER,
};

struct message {
	enum message_type type;
	union {
		struct {
			char *name;
		} login;
		struct {
			char *text;
		} login_err;
		struct {
			char *other;
			int red;
			int width;
			int height;
		} start_ok;
		struct {
			char *text;
		} start_err;
		struct {
			int column;
		} drop;
		struct {
			char *text;
		} drop_err;
		struct {
			int red;
			int column;
			int row;
		} notify_drop;
		struct {
			int red;
		} notify_over;
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

/* Writes a textual representation (including the trailing newline) of the message
 * to the buffer. This function will not mutate the message.
 *
 * On success returns 0, on error -1.
 */
int format_message(struct message *msg, struct buffer *buf);
