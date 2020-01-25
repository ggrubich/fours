#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "hashmap.h"
#include "buffer.h"
#include "protocol.h"
#include "side.h"
#include "game.h"

#define MAX_READ 64
#define MAX_WRITE 64

struct pair {
	struct client *red;
	struct client *blue;
	struct game game;
};

struct client {
	int sock;
	// NULL if client is not logged in
	char *name;
	// NULL if no pair
	struct pair *pair;
	struct buffer input;
	struct buffer output;
};

struct pair *pair_new(struct client *red, struct client *blue, int width, int height)
{
	struct pair *pair = malloc(sizeof(*pair));
	if (!pair) {
		return NULL;
	}
	pair->red = red;
	pair->blue = blue;
	game_init(&pair->game, width, height);
	red->pair = pair;
	blue->pair = pair;
	return pair;
}

void pair_free(struct pair *pair)
{
	pair->red->pair = NULL;
	pair->blue->pair = NULL;
	game_finalize(&pair->game);
	free(pair);
}

struct client *client_new(int sock)
{
	struct client *cli = malloc(sizeof(*cli));
	if (!cli) {
		return NULL;
	}
	cli->sock = sock;
	cli->name = NULL;
	cli->pair = NULL;
	buffer_init(&cli->input);
	buffer_init(&cli->output);
	return cli;
}

void client_free(struct client *cli)
{
	close(cli->sock);
	free(cli->name);
	if (cli->pair) {
		pair_free(cli->pair);
	}
	buffer_finalize(&cli->input);
	buffer_finalize(&cli->output);
	free(cli);
}

void client_free_(void *cli)
{
	client_free((struct client *)cli);
}

struct client *client_other(struct client *cli)
{
	if (!cli->pair) {
		return NULL;
	}
	return cli == cli->pair->red ? cli->pair->blue : cli->pair->red;
}

enum side client_side(struct client *cli)
{
	return cli == cli->pair->red ? SIDE_RED : SIDE_BLUE;
}

struct server {
	int epoll;
	int listener;
	// clients by file descrptior, maps int to struct client
	struct hashmap clients_by_fd;
	// clients by name, maps string to int
	struct hashmap fds_by_name;
	// fd of the client waiting for a game. -1 if empty.
	int waiting_client;

	// size of a game board
	int game_width;
	int game_height;
};

int make_listener(int port)
{
	int sock;
	struct sockaddr_in addr;
	int enable = 1;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		return -1;
	}
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
		goto error;
	}
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		goto error;
	}
	if (listen(sock, 5) < 0) {
		goto error;
	}
	return sock;
error:
	close(sock);
	return -1;
}

int server_init(struct server *s, int port, int game_width, int game_height)
{
	struct epoll_event event = {0};
	s->epoll = epoll_create1(0);
	if (s->epoll < 0) {
		return -1;
	}
	s->listener = make_listener(port);
	if (s->listener < 0) {
		return -1;
	}
	event.events = EPOLLIN;
	event.data.fd = s->listener;
	if (epoll_ctl(s->epoll, EPOLL_CTL_ADD, s->listener, &event) < 0) {
		close(s->listener);
		return -1;
	}
	hashmap_init(&s->clients_by_fd, &hashmap_ptr_equals, &hashmap_ptr_hash,
			NULL, &client_free_);
	hashmap_init(&s->fds_by_name, &hashmap_string_equals, &hashmap_string_hash,
			&free, NULL);
	s->waiting_client = -1;
	s->game_width = game_width;
	s->game_height = game_height;
	return 0;
}

void server_finalize(struct server *s)
{
	close(s->listener);
	hashmap_finalize(&s->clients_by_fd);
	hashmap_finalize(&s->fds_by_name);
}

int epoll_toggle_write(int epoll, int sock, int on)
{
	struct epoll_event event = {0};
	if (on) {
		event.events = EPOLLIN | EPOLLOUT;
	} else {
		event.events = EPOLLIN;
	}
	event.data.fd = sock;
	return epoll_ctl(epoll, EPOLL_CTL_MOD, sock, &event);
}

int respond(struct server *s, struct client *cli, struct message *msg)
{
	if (buffer_len(&cli->output) == 0) {
		if (epoll_toggle_write(s->epoll, cli->sock, 1) < 0) {
			return -1;
		}
	}
	return format_message(msg, &cli->output);
}

int respond_nullary(struct server *s, struct client *cli, enum message_type type)
{
	struct message resp;
	resp.type = type;
	return respond(s, cli, &resp);
}

int respond_err(struct server *s, struct client *cli, enum message_type type, char *text)
{
	struct message resp;
	resp.type = type;
	resp.data.err.text = text;
	return respond(s, cli, &resp);
}

int server_accept(struct server *s)
{
	struct client *cli;
	struct epoll_event event = {0};
	int sock = accept(s->listener, NULL, NULL);
	if (sock < 0) {
		return -1;
	}
	cli = client_new(sock);
	if (!cli) {
		close(sock);
		return -1;
	}
	if (hashmap_insert(&s->clients_by_fd, (void *)(intptr_t)sock, (void *)cli) < 0) {
		client_free(cli);
		return -1;
	}
	event.events = EPOLLIN;
	event.data.fd = sock;
	if (epoll_ctl(s->epoll, EPOLL_CTL_ADD, sock, &event) < 0) {
		hashmap_remove(&s->clients_by_fd, (void *)(intptr_t)sock);
		return -1;
	}
	printf("accepted client %d\n", sock);
	return 0;
}

int server_disconnect(struct server *s, struct client *cli)
{
	int sock = cli->sock;
	if (cli->name) {
		hashmap_remove(&s->fds_by_name, (void *)cli->name);
	}
	if (s->waiting_client == cli->sock) {
		s->waiting_client = -1;
	}
	if (cli->pair) {
		if (respond_nullary(s, client_other(cli), MSG_NOTIFY_QUIT) < 0) {
			return -1;
		}
	}
	hashmap_remove(&s->clients_by_fd, (void *)(intptr_t)sock);
	printf("client %d disconnected\n", sock);
	return 0;
}

int handle_login(struct server *s, struct client *cli, char *name)
{
	char *name1, *name2;
	if (cli->name) {
		return respond_err(s, cli, MSG_LOGIN_ERR, "user already logged in");
	}
	if (hashmap_contains(&s->fds_by_name, (void *)name)) {
		return respond_err(s, cli, MSG_LOGIN_ERR, "name already taken");
	}
	name1 = strdup(name);
	name2 = strdup(name);
	if (!name1 || !name2) {
		free(name1);
		free(name2);
		return -1;
	}
	if (hashmap_insert(&s->fds_by_name, (void *)name1, (void *)(intptr_t)cli->sock) < 0) {
		free(name1);
		free(name2);
		return -1;
	}
	cli->name = name2;
	return respond_nullary(s, cli, MSG_LOGIN_OK);
}

int respond_start_ok(struct server *s, struct client *cli)
{
	struct message resp;
	resp.type = MSG_START_OK;
	resp.data.start_ok.other = client_other(cli)->name;
	resp.data.start_ok.side = client_side(cli);
	resp.data.start_ok.width = cli->pair->game.width;
	resp.data.start_ok.height = cli->pair->game.height;
	resp.data.start_ok.red_undos = cli->pair->game.red_undos;
	resp.data.start_ok.blue_undos = cli->pair->game.blue_undos;
	return respond(s, cli, &resp);
}

int handle_start(struct server *s, struct client *cli)
{
	struct client *other;
	struct pair *pair;
	if (!cli->name) {
		return respond_err(s, cli, MSG_START_ERR, "not logged in");
	} else if (cli->pair) {
		return respond_err(s, cli, MSG_START_ERR, "already in a game");
	} else if (s->waiting_client == cli->sock) {
		return respond_err(s, cli, MSG_START_ERR, "already waiting for a game");
	}
	if (hashmap_get(&s->clients_by_fd,
			(void *)(uintptr_t)s->waiting_client,
			(void **)&other) < 0)
	{
		s->waiting_client = cli->sock;
		return 0;
	}
	pair = pair_new(cli, other, s->game_width, s->game_height);
	if (!pair) {
		return -1;
	}
	s->waiting_client = -1;
	if (respond_start_ok(s, cli) < 0) {
		return -1;
	}
	if (respond_start_ok(s, other) < 0) {
		return -1;
	}
	return 0;
}

int handle_drop(struct server *s, struct client *cli, int column)
{
	struct message resp;
	struct game *game;
	int row;
	enum side side;
	struct client *other;
	if (!cli->pair) {
		return respond_err(s, cli, MSG_DROP_ERR, "not in game right now");
	}
	game = &cli->pair->game;
	side = client_side(cli);
	if ((row = game_drop(game, side, column)) < 0) {
		return respond_err(s, cli, MSG_DROP_ERR, "can't drop here and now");
	}
	if (respond_nullary(s, cli, MSG_DROP_OK) < 0) {
		return -1;
	}
	other = client_other(cli);
	resp.type = MSG_NOTIFY_DROP;
	resp.data.notify_drop.side = side;
	resp.data.notify_drop.column = column;
	resp.data.notify_drop.row = row;
	if (respond(s, cli, &resp) < 0) {
		return -1;
	}
	if (respond(s, other, &resp) < 0) {
		return -1;
	}
	if (game->over) {
		resp.type = MSG_NOTIFY_OVER;
		resp.data.notify_over.winner = game->winner;
		pair_free(cli->pair);
		if (respond(s, cli, &resp) < 0) {
			return -1;
		}
		if (respond(s, other, &resp) < 0) {
			return -1;
		}
	}
	return 0;
}

int handle_undo(struct server *s, struct client *cli)
{
	struct message resp;
	int column, row;
	enum side side;
	struct client *other;
	if (!cli->pair) {
		return respond_err(s, cli, MSG_DROP_ERR, "not in game right now");
	}
	side = client_side(cli);
	if (game_undo(&cli->pair->game, side, &column, &row) < 0) {
		return respond_err(s, cli, MSG_DROP_ERR, "can't undo here and now");
	}
	if (respond_nullary(s, cli, MSG_UNDO_OK) < 0) {
		return -1;
	}
	other = client_other(cli);
	resp.type = MSG_NOTIFY_UNDO;
	resp.data.notify_undo.side = side;
	resp.data.notify_undo.column = column;
	resp.data.notify_undo.row = row;
	if (respond(s, cli, &resp) < 0) {
		return -1;
	}
	if (respond(s, other, &resp) < 0) {
		return -1;
	}
	return 0;
}

int handle_quit(struct server *s, struct client *cli)
{
	struct client *other;
	if (cli->pair) {
		other = client_other(cli);
		pair_free(cli->pair);
		if (respond_nullary(s, other, MSG_NOTIFY_QUIT) < 0) {
			return -1;
		}
	} else if (s->waiting_client == cli->sock) {
		s->waiting_client = -1;
	} else {
		return respond_err(s, cli, MSG_QUIT_ERR, "not in game or queue now");
	}
	if (respond_nullary(s, cli, MSG_QUIT_OK) < 0) {
		return -1;
	}
	return 0;
}

int handle_message(struct server *s, struct client *cli, struct message *msg)
{
	struct message resp;
	switch (msg->type) {
	case MSG_LOGIN:
		return handle_login(s, cli, msg->data.login.name);
	case MSG_START:
		return handle_start(s, cli);
	case MSG_DROP:
		return handle_drop(s, cli, msg->data.drop.column);
	case MSG_UNDO:
		return handle_undo(s, cli);
	case MSG_QUIT:
		return handle_quit(s, cli);
	default:
		resp.type = MSG_INVALID;
		return respond(s, cli, &resp);
	}
}

int server_read(struct server *s, struct client *cli)
{
	char buf[MAX_READ];
	int i, n;
	struct message msg;
	n = read(cli->sock, buf, sizeof(buf));
	if (n < 0) {
		return -1;
	}
	if (n == 0) {
		return server_disconnect(s, cli);
	}
	if (buffer_push(&cli->input, buf, n) < 0) {
		return -1;
	}
	while ((n = parse_message(&cli->input, &msg)) != 0) {
		if (n < 0) {
			n = -n;
			printf("invalid message: ");
			for (i = 0; i < n; ++i) {
				printf("%c", buffer_get(&cli->input, i));
			}
			msg.type = MSG_INVALID;
			if (respond(s, cli, &msg) < 0) {
				return -1;
			}
		} else {
			printf("message: ");
			for (i = 0; i < n; ++i) {
				printf("%c", buffer_get(&cli->input, i));
			}
			if (handle_message(s, cli, &msg) < 0) {
				close_message(&msg);
				return -1;
			}
		}
		close_message(&msg);
		buffer_pop(&cli->input, NULL, n);
	}
	return 0;
}

int server_write(struct server *s, struct client *cli)
{
	char buf[MAX_WRITE];
	size_t len;
	int n;
	len = sizeof(buf) < buffer_len(&cli->output)
		? sizeof(buf)
		: buffer_len(&cli->output);
	buffer_peek(&cli->output, buf, len);
	n = write(cli->sock, buf, len);
	if (n < 0 && errno == EPIPE) {
		errno = 0;
		return server_disconnect(s, cli);
	}
	if (n < 0) {
		return -1;
	}
	buffer_pop(&cli->output, NULL, n);
	if (buffer_len(&cli->output) == 0) {
		if (epoll_toggle_write(s->epoll, cli->sock, 0) < 0) {
			return -1;
		}
	}
	return 0;
}

#define MAX_EVENTS 32

int with_client(struct server *s, int sock, int (*fn)(struct server *, struct client *))
{
	struct client *cli;
	if (hashmap_get(&s->clients_by_fd, (void *)(intptr_t)sock, (void **)&cli) < 0) {
		return 0;
	}
	return fn(s, cli);
}

int server_run(struct server *s)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds;
	int i;
	while (1) {
		nfds = epoll_wait(s->epoll, events, MAX_EVENTS, -1);
		if (nfds < 0) {
			return -1;
		}
		for (i = 0; i < nfds; ++i) {
			if (events[i].data.fd == s->listener) {
				if (server_accept(s) < 0) {
					return -1;
				}
				continue;
			}
			if (events[i].events & EPOLLIN) {
				if (with_client(s, events[i].data.fd, server_read) < 0) {
					return -1;
				}
			}
			if (events[i].events & EPOLLOUT) {
				if (with_client(s, events[i].data.fd, server_write) < 0) {
					return -1;
				}
			}
		}
	}
	return 0;
}

const int DEFAULT_PORT = 8051;
const int DEFAULT_WIDTH = 7;
const int DEFAULT_HEIGHT = 6;

const char *USAGE = "server [-p PORT] [-c COLUMNS] [-r ROWS]";

int parse_natural(char *str)
{
	char *endptr;
	int n = strtol(str, &endptr, 10);
	if (*str == '\0' || *endptr != '\0') {
		return -1;
	}
	return n;
}

int parse_args(int argc, char **argv, int *port, int *width, int *height)
{
	int c;
	*port = DEFAULT_PORT;
	*width = DEFAULT_WIDTH;
	*height = DEFAULT_HEIGHT;
	while ((c = getopt(argc, argv, "p:w:h:")) != -1) {
		switch (c) {
		case 'p':
			if ((*port = parse_natural(optarg)) < 0) {
				return -1;
			}
			break;
		case 'w':
			if ((*width = parse_natural(optarg)) < 0) {
				return -1;
			}
			break;
		case 'h':
			if ((*height = parse_natural(optarg)) < 0) {
				return -1;
			}
			break;
		default:
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int port, width, height;
	struct server srv;
	if (parse_args(argc, argv, &port, &width, &height) < 0) {
		fprintf(stderr, "invalid arguments\n");
		fprintf(stderr, "%s\n", USAGE);
		return 1;
	}
	if (server_init(&srv, port, width, height) < 0) {
		perror("failed to initialize server");
		return 1;
	}
	if (server_run(&srv) < 0) {
		perror("server error");
		server_finalize(&srv);
		return 1;
	}
	server_finalize(&srv);
	return 0;
}
