#pragma once

#include <stddef.h>

#include "buffer.h"
#include "side.h"

enum message_type {
	// MSG_INVALID is sent when the server couldn't parse the message
	// or if it does not know how to handle it.
	MSG_INVALID,

	// MSG_LOGIN is sent to the server in order to log in with the given name.
	// The server will respond with MSG_LOGIN_OK or MSG_LOGIN_ERR.
	MSG_LOGIN,
	MSG_LOGIN_OK,
	MSG_LOGIN_ERR,

	// MSG_START when sent to the server will attempt to connect a client
	// to a new game. The server will respond with MSG_START_OK when
	// the game starts or with START_ERR if game can't be started.
	MSG_START,
	MSG_START_OK,
	MSG_START_ERR,

	// MSG_DROP tries to drop the disc at the given column. The server
	// will respond with MSG_DROP_ERR on error or MSG_DROP_OK
	// followed by MSG_NOTIFY_DROP on success.
	MSG_DROP,
	MSG_DROP_OK,
	MSG_DROP_ERR,

	// MSG_UNDO tries to undo the last move. The server
	// will respond with MSG_UNDO_ERR on error or MSG_UNDO_OK
	// followed by MSG_NOTIFY_UNDO on success.
	MSG_UNDO,
	MSG_UNDO_OK,
	MSG_UNDO_ERR,

	// MSG_QUIT quits the current game, or leaves the queue initiated by MSG_START.
	// Server responds with MSG_QUIT_OK or MSG_QUIT_ERR.
	MSG_QUIT,
	MSG_QUIT_OK,
	MSG_QUIT_ERR,

	// MSG_NOTIFY_DROP and MSG_NOTIFY_UNDO are sent to the client when
	// a disc is dropped or a move is undone.
	MSG_NOTIFY_DROP,
	MSG_NOTIFY_UNDO,
	// MSG_NOTIFY_OVER is sent to the client when the game ends.
	MSG_NOTIFY_OVER,
	// MSG_NOTIFY_QUIT is sent to the client when opponent quits the game.
	MSG_NOTIFY_QUIT,
};

struct message {
	enum message_type type;
	union {
		struct {
			char *text;
		} err;

		struct {
			char *name;
		} login;

		struct {
			// opponent's name
			char *other;
			// our side
			enum side side;
			// size of the board
			int width;
			int height;
			// initial undos for each player
			int red_undos;
			int blue_undos;
		} start_ok;

		struct {
			int column;
		} drop;

		// Note that rows here are counted from the bottom,
		// i.e. row 0 is the bottommost row.
		struct {
			enum side side;
			int column;
			int row;
		} notify_drop;
		struct {
			enum side side;
			int column;
			int row;
		} notify_undo;
		struct {
			enum side winner;
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
