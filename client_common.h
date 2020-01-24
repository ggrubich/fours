#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>

#include "buffer.h"
#include "protocol.h"

enum {
	RES_OK = 0,
	RES_ERR = -1,
	RES_QUIT = -2,
	RES_INVALID_ARGS = -3,
	RES_DISCONNECT = -4,
	RES_INVALID_MSG = -5,
};

enum event_type {
	EVENT_INPUT,
	EVENT_MSG,
};

struct event {
	enum event_type type;
	union {
		int ch;
		struct message *msg;
	} data;
};

enum client_state {
	STATE_LOGIN_WAIT,
	STATE_LOGIN_ERR,
	STATE_LOBBY,
	STATE_START_WAIT,
	STATE_GAME,
	STATE_GAME_QUIT,
	STATE_HALTED,
};

enum {
	LOBBY_START,
	LOBBY_QUIT,
	LOBBY_LEN,
};

enum {
	GAME_QUIT_YES,
	GAME_QUIT_NO,
	GAME_QUIT_LEN,
};

struct client {
	int (*handle)(struct client *, struct event *);
	int (*render)(struct client *);

	int epoll;

	struct sockaddr_in addr;
	char *name;
	int conn;
	struct buffer input;
	struct buffer output;

	enum client_state state;
	union {
		char *login_err;

		struct {
			int index;
		} lobby;

		struct {
			int index;
		} game_quit;
	} data;
};

int request(struct client *c, struct message *msg);

int client_init(struct client *c, struct sockaddr_in addr, char *name);

void client_finalize(struct client *c);

int client_run(struct client *c);
