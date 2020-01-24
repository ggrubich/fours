#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "buffer.h"
#include "protocol.h"

#define BUF_SIZE 512

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
};

struct client {
	int epoll;

	struct sockaddr_in addr;
	char *name;
	int conn;
	struct buffer input;
	struct buffer output;

	enum client_state state;
	union {
		char *login_err;
	} data;
};

int request(struct client *c, struct message *msg)
{
	struct epoll_event event = {0};
	if (buffer_len(&c->output) == 0) {
		event.events = EPOLLIN | EPOLLOUT;
		event.data.fd = c->conn;
		if (epoll_ctl(c->epoll, EPOLL_CTL_MOD, c->conn, &event) < 0) {
			return RES_ERR;
		}
	}
	if (format_message(msg, &c->output) < 0) {
		return RES_ERR;
	}
	return RES_OK;
}

int client_connect(struct client *c)
{
	struct epoll_event event = {0};
	struct message msg;
	int res;
	c->conn = socket(AF_INET, SOCK_STREAM, 0);
	if (c->conn < 0) {
		return RES_ERR;
	}
	if (connect(c->conn, (struct sockaddr *)&c->addr, sizeof(c->addr)) < 0) {
		close(c->conn);
		return RES_ERR;
	}
	event.events = EPOLLIN;
	event.data.fd = c->conn;
	if (epoll_ctl(c->epoll, EPOLL_CTL_ADD, c->conn, &event) < 0) {
		close(c->conn);
		return RES_ERR;
	}
	msg.type = MSG_LOGIN;
	msg.data.login.name = c->name;
	if ((res = request(c, &msg)) < 0) {
		close(c->conn);
		return res;
	}
	c->state = STATE_LOGIN_WAIT;
	return RES_OK;
}

int client_init(struct client *c, struct sockaddr_in addr, char *name)
{
	struct epoll_event event = {0};
	int res;
	c->epoll = epoll_create1(0);
	if (c->epoll < 0) {
		return RES_ERR;
	}
	event.events = EPOLLIN;
	event.data.fd = STDIN_FILENO;
	if (epoll_ctl(c->epoll, EPOLL_CTL_ADD, STDIN_FILENO, &event) < 0) {
		return RES_ERR;
	}
	c->addr = addr;
	c->name = name;
	buffer_init(&c->input);
	buffer_init(&c->output);
	if ((res = client_connect(c)) < 0) {
		return res;
	}
	initscr();
	noecho();
	keypad(stdscr, TRUE);
	return RES_OK;
}

void client_finalize(struct client *c)
{
	free(c->name);
	close(c->conn);
	buffer_finalize(&c->input);
	buffer_finalize(&c->output);
	endwin();
	switch (c->state) {
	case STATE_LOGIN_ERR:
		free(c->data.login_err);
		break;
	default:
		break;
	}
}

int handle_login_wait(struct client *c, struct event *ev)
{
	struct message *msg;
	if (ev->type == EVENT_MSG) {
		msg = ev->data.msg;
		switch (msg->type) {
		case MSG_LOGIN_OK:
			c->state = STATE_LOBBY;
			break;
		case MSG_LOGIN_ERR:
			c->state = STATE_LOGIN_ERR;
			c->data.login_err = strdup(msg->data.err.text);
			if (!c->data.login_err) {
				return RES_ERR;
			}
			break;
		default:
			break;
		}
	}
	return RES_OK;
}

int handle_login_err(struct client *c, struct event *ev)
{
	// TODO do something
	return RES_OK;
}

int handle_lobby(struct client *c, struct event *ev)
{
	// TODO do something
	return RES_OK;
}

int handle(struct client *c, struct event *ev)
{
	switch (c->state) {
	case STATE_LOGIN_WAIT:
		return handle_login_wait(c, ev);
	case STATE_LOGIN_ERR:
		return handle_login_err(c, ev);
	case STATE_LOBBY:
		return handle_lobby(c, ev);
	}
	return RES_OK;
}

int render_login_wait(struct client *c)
{
	move(0, 0);
	printw("logging in...\n");
	return RES_OK;
}

int render_login_err(struct client *c)
{
	// TODO better error rendering
	move(0, 0);
	printw("login error: %s\n", c->data.login_err);
	return RES_OK;
}

int render_lobby(struct client *c)
{
	// TODO render the menu
	move(0, 0);
	printw("lobby should be here\n");
	return RES_OK;
}

int render(struct client *c)
{
	switch (c->state) {
	case STATE_LOGIN_WAIT:
		return render_login_wait(c);
	case STATE_LOGIN_ERR:
		return render_login_err(c);
	case STATE_LOBBY:
		return render_lobby(c);
	}
	return RES_OK;
}

int client_getch(struct client *c)
{
	int ch = getch();
	struct event ev;
	ev.type = EVENT_INPUT;
	ev.data.ch = ch;
	return handle(c, &ev);
}

int client_read(struct client *c)
{
	char buf[BUF_SIZE];
	int n, res;
	struct message msg;
	struct event ev;
	n = read(c->conn, buf, sizeof(buf));
	if (n < 0) {
		return RES_ERR;
	}
	if (n == 0) {
		return RES_DISCONNECT;
	}
	if (buffer_push(&c->input, buf, n) < 0) {
		return RES_ERR;
	}
	while ((n = parse_message(&c->input, &msg)) != 0) {
		if (n < 0) {
			close_message(&msg);
			return RES_INVALID_MSG;
		}
		ev.type = EVENT_MSG;
		ev.data.msg = &msg;
		if ((res = handle(c, &ev)) < 0) {
			close_message(&msg);
			return res;
		}
		close_message(&msg);
		buffer_pop(&c->input, NULL, n);
	}
	return RES_OK;
}

int client_write(struct client *c)
{
	char buf[BUF_SIZE];
	size_t len;
	int n;
	struct epoll_event event = {0};
	len = sizeof(buf) < buffer_len(&c->output)
		? sizeof(buf)
		: buffer_len(&c->output);
	buffer_peek(&c->output, buf, len);
	n = write(c->conn, buf, len);
	if (n < 0) {
		return RES_ERR;
	}
	buffer_pop(&c->output, NULL, n);
	if (buffer_len(&c->output) == 0) {
		event.events = EPOLLIN;
		event.data.fd = c->conn;
		if (epoll_ctl(c->epoll, EPOLL_CTL_MOD, c->conn, &event) < 0) {
			return RES_ERR;
		}
	}
	return RES_OK;
}

#define MAX_EVENTS 32

int client_run(struct client *c)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds;
	int i;
	int res;
	if ((res = render(c)) < 0) {
		return res;
	}
	refresh();
	while (1) {
		nfds = epoll_wait(c->epoll, events, MAX_EVENTS, -1);
		if (nfds < 0) {
			return RES_ERR;
		}
		for (i = 0; i < nfds; ++i) {
			if (events[i].data.fd == STDIN_FILENO) {
				if ((res = client_getch(c)) < 0) {
					return res;
				}
			} else if (events[i].data.fd == c->conn) {
				if (events[i].events & EPOLLIN) {
					if ((res = client_read(c)) < 0) {
						return res;
					}
				}
				if (events[i].events & EPOLLOUT) {
					if ((res = client_write(c)) < 0) {
						return res;
					}
				}
			}
			if ((res = render(c)) < 0) {
				return res;
			}
			refresh();
		}
	}
	return 0;
}

const char *USAGE = "client HOST[:PORT] NAME";

const int DEFAULT_PORT = 8080;

int parse_args(int argc, char **argv, struct sockaddr_in *addr, char **name)
{
	char *host, *colon, *portptr, *endptr;
	int port;
	if (argc != 3) {
		return RES_INVALID_ARGS;
	}
	host = strdup(argv[1]);
	if (!host) {
		return RES_ERR;
	}
	colon = strchr(host, ':');
	if (colon) {
		host[colon - host] = '\0';
		portptr = colon + 1;
		port = strtol(portptr, &endptr, 10);
		if (*portptr == '\0' || *endptr != '\0') {
			free(host);
			return RES_INVALID_ARGS;
		}
	} else {
		port = DEFAULT_PORT;
	}
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);
	if (inet_pton(AF_INET, host, &addr->sin_addr) == 0) {
		free(host);
		return RES_INVALID_ARGS;
	}
	*name = strdup(argv[2]);
	if (!*name) {
		free(host);
		return RES_ERR;
	}
	free(host);
	return RES_OK;
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	char *name;
	struct client c;
	int res;
	if ((res = parse_args(argc, argv, &addr, &name)) < 0) {
		if (res == RES_INVALID_ARGS) {
			fprintf(stderr, "invalid arguments\n");
		} else {
			perror("error parsing args");
		}
		return 1;
	}
	if ((res = client_init(&c, addr, name)) < 0) {
		perror("failed to initialize the client");
		free(name);
		return 1;
	}
	res = client_run(&c);
	client_finalize(&c);
	switch (res) {
	case RES_ERR:
		perror("client error");
		return 1;
	case RES_QUIT:
		return 0;
	case RES_DISCONNECT:
		fprintf(stderr, "disconnected by the server\n");
		return 1;
	case RES_INVALID_MSG:
		fprintf(stderr, "server sent an invalid message\n");
		return 1;
	}
	return 0;
}
