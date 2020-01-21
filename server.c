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
#include "game.h"

#define MAX_READ 64
#define MAX_WRITE 64

#define GRID_WIDTH 7
#define GRID_HEIGHT 6

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

struct server {
	int epoll;
	int listener;
	// clients by file descrptior, maps int to struct client
	struct hashmap clients_by_fd;
	// clients by name, maps string to int
	struct hashmap fds_by_name;
	// fd of the client waiting for a game. -1 if empty.
	int waiting_client;
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

int server_init(struct server *s, int port)
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
	return 0;
}

void server_finalize(struct server *s)
{
	close(s->listener);
	hashmap_finalize(&s->clients_by_fd);
	hashmap_finalize(&s->fds_by_name);
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
	hashmap_remove(&s->clients_by_fd, (void *)(intptr_t)sock);
	printf("client %d disconnected\n", sock);
	return 0;
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

int handle_login(struct server *s, struct client *cli, char *name)
{
	char *name1, *name2;
	struct message resp;
	if (cli->name) {
		resp.type = MSG_LOGIN_ERR;
		resp.data.login_err.text = "user already logged in";
		return respond(s, cli, &resp);
	}
	if (hashmap_contains(&s->fds_by_name, (void *)name)) {
		resp.type = MSG_LOGIN_ERR;
		resp.data.login_err.text = "name already taken";
		return respond(s, cli, &resp);
	}
	name1 = strdup(name);
	name2 = strdup(name);
	if (!name1 || !name2) {
		free(name1);
		free(name2);
		return -1;
	}
	hashmap_insert(&s->fds_by_name, (void *)name1, (void *)(intptr_t)cli->sock);
	cli->name = name2;
	resp.type = MSG_LOGIN_OK;
	return respond(s, cli, &resp);
}

int respond_start_ok(struct server *s, struct client *cli)
{
	struct message resp;
	resp.type = MSG_START_OK;
	int red = cli == cli->pair->red;
	struct client *other = red ? cli->pair->blue : cli->pair->red;
	resp.data.start_ok.other = other->name;
	resp.data.start_ok.red = red;
	resp.data.start_ok.width = cli->pair->game.width;
	resp.data.start_ok.height = cli->pair->game.height;
	return respond(s, cli, &resp);
}

int handle_start(struct server *s, struct client *cli)
{
	struct message resp;
	struct client *other;
	struct pair *pair;
	resp.type = MSG_START_ERR;
	resp.data.start_err.text = NULL;
	if (!cli->name) {
		resp.data.start_err.text = "not logged in";
	} else if (cli->pair) {
		resp.data.start_err.text = "already in a game";
	} else if (s->waiting_client == cli->sock) {
		resp.data.start_err.text = "already waiting for a game";
	}
	if (resp.data.start_err.text) {
		return respond(s, cli, &resp);
	}
	if (hashmap_get(&s->clients_by_fd,
			(void *)(uintptr_t)s->waiting_client,
			(void **)&other) < 0)
	{
		s->waiting_client = cli->sock;
		return 0;
	}
	pair = pair_new(cli, other, GRID_WIDTH, GRID_HEIGHT);
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
	int color;
	struct client *other;
	if (!cli->pair) {
		resp.type = MSG_DROP_ERR;
		resp.data.drop_err.text = "not in game right now";
		return respond(s, cli, &resp);
	}
	game = &cli->pair->game;
	color = cli == cli->pair->red ? GAME_RED : GAME_BLUE;
	if ((row = game_drop(game, color, column)) < 0) {
		resp.type = MSG_DROP_ERR;
		resp.data.drop_err.text = "can't drop here and now";
		return respond(s, cli, &resp);
	}
	resp.type = MSG_DROP_OK;
	if (respond(s, cli, &resp) < 0) {
		return -1;
	}
	other = cli == cli->pair->red ? cli->pair->blue : cli->pair->red;
	resp.type = MSG_NOTIFY_DROP;
	resp.data.notify_drop.red = color;
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
		resp.data.notify_over.red = color;
		if (respond(s, cli, &resp) < 0) {
			return -1;
		}
		if (respond(s, other, &resp) < 0) {
			return -1;
		}
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

int main(int argc, char **argv)
{
	struct server srv;
	if (server_init(&srv, 8080) < 0) {
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
