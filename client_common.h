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
	STATE_GAME_OVER,
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

struct game_base {
	char *other;
	enum side side;
	enum side **board;
	int width;
	int height;
	int column;
	enum side turn;
	int red_undos;
	int blue_undos;
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
			struct game_base b;
		} game;
		struct {
			struct game_base b;
			int index;
		} game_quit;
		struct {
			struct game_base b;
			enum side winner;
		} game_over;
	} data;
};

int request(struct client *c, struct message *msg);

int client_init(struct client *c, struct sockaddr_in addr, char *name);

void finalize_state(struct client *c);

void client_finalize(struct client *c);

int client_run(struct client *c);
