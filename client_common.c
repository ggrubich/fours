#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ncurses.h>
#include <sys/epoll.h>

#include "client_common.h"
#include "client_handle.h"
#include "client_render.h"

#define BUF_SIZE 512

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

static int client_connect(struct client *c)
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
	curs_set(0);
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


static int client_getch(struct client *c)
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

static int client_write(struct client *c)
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

static int client_redraw(struct client *c)
{
	int res;
	clear();
	if ((res = render(c)) < 0) {
		return res;
	}
	refresh();
	return RES_OK;
}

#define MAX_EVENTS 32

int client_run(struct client *c)
{
	struct epoll_event events[MAX_EVENTS];
	int nfds;
	int i;
	int res;
	if ((res = client_redraw(c)) < 0) {
		return res;
	}
	while (1) {
		nfds = epoll_wait(c->epoll, events, MAX_EVENTS, -1);
		if (nfds < 0) {
			if (errno == EINTR) {
				// TODO handle SIGWINCH properly. for now
				// we just catch it at epoll_wait, but it can
				// also interrupt other syscalls and we should
				// probably do something about it.
				if ((res = client_redraw(c)) < 0) {
					return res;
				}
				errno = 0;
				continue;
			}
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
			if ((res = client_redraw(c)) < 0) {
				return res;
			}
		}
	}
	return 0;
}
