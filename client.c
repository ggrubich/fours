#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/epoll.h>

#include "buffer.h"
#include "protocol.h"

#define BUF_SIZE 512

enum {
	RES_OK = 0,
	RES_ERR = -1,
	RES_QUIT = -2,
	RES_DISCONNECT = -3,
	RES_INVALID_MSG = -4,
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

struct client {
	int epoll;
	int conn;
	struct buffer input;
	struct buffer output;

	int lastch;
};

int client_init(struct client *c)
{
	struct epoll_event event = {0};
	c->epoll = epoll_create1(0);
	if (c->epoll < 0) {
		return RES_ERR;
	}
	event.events = EPOLLIN;
	event.data.fd = STDIN_FILENO;
	if (epoll_ctl(c->epoll, EPOLL_CTL_ADD, STDIN_FILENO, &event) < 0) {
		return RES_ERR;
	}
	// TODO connect
	buffer_init(&c->input);
	buffer_init(&c->output);
	initscr();
	noecho();
	keypad(stdscr, TRUE);
	return RES_OK;
}

void client_finalize(struct client *c)
{
	close(c->conn);
	buffer_finalize(&c->input);
	buffer_finalize(&c->output);
	endwin();
}

int handle(struct client *c, struct event *ev)
{
	if (ev->type == EVENT_INPUT) {
		if (ev->data.ch == 'q') {
			return RES_QUIT;
		}
		c->lastch = ev->data.ch;
	}
	return RES_OK;
}

int render(struct client *c)
{
	move(10, 10);
	switch (c->lastch) {
	case KEY_UP:
		printw("up\n");
		break;
	case KEY_DOWN:
		printw("down\n");
		break;
	case KEY_LEFT:
		printw("left\n");
		break;
	case KEY_RIGHT:
		printw("right\n");
		break;
	default:
		printw("other %c\n", c->lastch);
	}
	refresh();
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
				if (events[i].events & EPOLLIN) {
					if ((res = client_write(c)) < 0) {
						return res;
					}
				}
			}
			if ((res = render(c)) < 0) {
				return res;
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct client c;
	int res;
	if ((res = client_init(&c)) < 0) {
		perror("failed to initialize the client");
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
