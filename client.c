#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ncurses.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "buffer.h"
#include "protocol.h"

#define BUF_SIZE 512

#define KEY_ESC 27

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

const char *LOBBY[] = {"START", "QUIT"};

enum {
	LOBBY_START,
	LOBBY_QUIT,
	LOBBY_LEN,
};

const char *GAME_QUIT[] = {"YES", "NO"};

enum {
	GAME_QUIT_YES,
	GAME_QUIT_NO,
	GAME_QUIT_LEN,
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

		struct {
			int index;
		} lobby;

		struct {
			int index;
		} game_quit;
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

int goto_lobby(struct client *c)
{
	c->state = STATE_LOBBY;
	c->data.lobby.index = 0;
	return RES_OK;
}

int handle_login_wait(struct client *c, struct event *ev)
{
	struct message *msg;
	if (ev->type == EVENT_MSG) {
		msg = ev->data.msg;
		switch (msg->type) {
		case MSG_LOGIN_OK:
			return goto_lobby(c);
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
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_ENTER:
		case '\n':
			return RES_QUIT;
		default:
			break;
		}
	}
	return RES_OK;
}

int handle_lobby(struct client *c, struct event *ev)
{
	struct message req;
	int res;
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_UP:
			if (c->data.lobby.index != 0) {
				--c->data.lobby.index;
			}
			break;
		case KEY_DOWN:
			if (c->data.lobby.index + 1 < LOBBY_LEN) {
				++c->data.lobby.index;
			}
			break;
		case KEY_ENTER:
		case '\n':
			switch (c->data.lobby.index) {
			case LOBBY_START:
				req.type = MSG_START;
				if ((res = request(c, &req)) < 0) {
					return res;
				}
				c->state = STATE_START_WAIT;
				break;
			case LOBBY_QUIT:
				return RES_QUIT;
			default:
				break;
			}
		default:
			break;
		}
	}
	return RES_OK;
}

int handle_start_wait(struct client *c, struct event *ev)
{
	struct message req;
	struct message *msg;
	int res;
	if (ev->type == EVENT_INPUT && (ev->data.ch == KEY_ENTER || ev->data.ch == '\n')) {
		req.type = MSG_QUIT;
		if ((res =  request(c, &req)) < 0) {
			return res;
		}
		return goto_lobby(c);
	} else if (ev->type == EVENT_MSG) {
		msg = ev->data.msg;
		switch (msg->type) {
		case MSG_START_ERR:
			// should not happen anyway
			return RES_INVALID_MSG;
		case MSG_START_OK:
			c->state = STATE_GAME;
			break;
		default:
			break;
		}
	}
	return RES_OK;
}

int handle_game(struct client *c, struct event *ev)
{
	struct message *msg;
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_ESC:
			c->state = STATE_GAME_QUIT;
			c->data.game_quit.index = 0;
			break;
		default:
			break;
		}
	} else if (ev->type == EVENT_MSG) {
		msg = ev->data.msg;
		switch (msg->type) {
		case MSG_NOTIFY_QUIT:
			c->state = STATE_HALTED;
			break;
		default:
			break;
		}
	}
	return RES_OK;
}

int handle_game_quit(struct client *c, struct event *ev)
{
	struct message *msg;
	struct message req;
	int res;
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_UP:
			if (c->data.game_quit.index != 0) {
				--c->data.game_quit.index;
			}
			break;
		case KEY_DOWN:
			if (c->data.game_quit.index < GAME_QUIT_LEN) {
				++c->data.game_quit.index;
			}
			break;
		case KEY_ENTER:
		case '\n':
			if (c->data.game_quit.index == GAME_QUIT_YES) {
				req.type = MSG_QUIT;
				if ((res = request(c, &req)) < 0) {
					return res;
				}
				goto_lobby(c);
			} else {
				c->state = STATE_GAME;
			}
			break;
		}
	} else if (ev->type == EVENT_MSG) {
		msg = ev->data.msg;
		switch (msg->type) {
		case MSG_NOTIFY_QUIT:
			c->state = STATE_HALTED;
			break;
		default:
			break;
		}
	}
	return RES_OK;
}

int handle_halted(struct client *c, struct event *ev)
{
	if (ev->type == EVENT_INPUT && (ev->data.ch == KEY_ENTER || ev->data.ch == '\n')) {
		goto_lobby(c);
	}
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
	case STATE_START_WAIT:
		return handle_start_wait(c, ev);
	case STATE_GAME:
		return handle_game(c, ev);
	case STATE_GAME_QUIT:
		return handle_game_quit(c, ev);
	case STATE_HALTED:
		return handle_halted(c, ev);
	}
	return RES_OK;
}

void padded(const char *text, int left, int right)
{
	int i;
	for (i = 0; i < left; ++i) {
		addch(' ');
	}
	printw("%s", text);
	for (i = 0; i < right; ++i) {
		addch(' ');
	}
}

void centered(const char *text, int width)
{
	int pad = width - strlen(text);
	int left = pad / 2;
	int right = pad - left;
	padded(text, left, right);
}

const int MENU_PAD = 6;

void render_menu(const char **items, int len, int current)
{
	int height, width;
	int i;
	int item_width = 0;
	int startx, starty;
	for (i = 0; i < len; ++i) {
		if (strlen(items[i]) > item_width) {
			item_width = strlen(items[i]);
		}
	}
	item_width += MENU_PAD;
	getmaxyx(stdscr, height, width);
	if (len > height || item_width > width) {
		move(0, 0);
		printw("screen to small\n");
		return;
	}
	starty = (height - len) / 2;
	startx = (width - item_width) / 2;
	for (i = 0; i < len; ++i) {
		if (i == current) {
			attron(A_REVERSE);
		}
		move(starty + i, startx);
		padded(items[i], 1, item_width - strlen(items[i]) - 1);
		if (i == current) {
			attroff(A_REVERSE);
		}
	}
}

void frame(int width, int height)
{
	int y, x;
	getyx(stdscr, y, x);
	mvhline(y, x, 0, width);
	mvhline(y + height - 1, x, 0, width);
	mvvline(y, x, 0, height);
	mvvline(y, x + width - 1, 0, height);
	mvaddch(y, x, ACS_ULCORNER);
	mvaddch(y, x + width - 1, ACS_URCORNER);
	mvaddch(y + height - 1, x, ACS_LLCORNER);
	mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
}

const int DIALOG_PAD = 2;

int dialog_width(const char *title, const char *text, const char **choices, int len)
{
	int i;
	int width = strlen(title);
	if (strlen(text) > width) {
		width = strlen(text);
	}
	for (i = 0; i < len; ++i) {
		if (strlen(choices[i]) > width) {
			width = strlen(choices[i]);
		}
	}
	return width + (2 * DIALOG_PAD);
}

void render_dialog(const char *title, const char *text,
		const char **choices, int len, int current)
{
	int i;
	int scr_height, scr_width;
	int height, width;
	int starty, startx;
	getmaxyx(stdscr, scr_height, scr_width);
	height = 7 + len;
	width = dialog_width(title, text, choices, len);
	starty = (scr_height - height) / 2;
	startx = (scr_width - width) / 2;
	move(starty, startx);
	frame(width, height);
	move(starty + 1, startx + DIALOG_PAD);
	attron(A_BOLD);
	centered(title, width - (2 * DIALOG_PAD));
	attroff(A_BOLD);
	move(starty + 3, startx + DIALOG_PAD);
	centered(text, width - (2 * DIALOG_PAD));
	for (i = 0; i < len; ++i) {
		move(starty + 5 + i, startx + DIALOG_PAD);
		if (i == current) {
			attron(A_REVERSE);
		}
		centered(choices[i], width - (2 * DIALOG_PAD));
		if (i == current) {
			attroff(A_REVERSE);
		}
	}
}

int render_login_wait(struct client *c)
{
	move(0, 0);
	printw("logging in...\n");
	return RES_OK;
}

int render_login_err(struct client *c)
{
	char *choices[] = {"OK"};
	render_dialog("Error", c->data.login_err, (const char **)choices, 1, 0);
	return RES_OK;
}

int render_lobby(struct client *c)
{
	render_menu(LOBBY, LOBBY_LEN, c->data.lobby.index);
	return RES_OK;
}

int render_start_wait(struct client *c)
{
	char *choices[] = {"CANCEL"};
	render_dialog("Starting...",
			"Waiting for the second player to join",
			(const char **)choices,
			1,
			0);
	return RES_OK;
}

int render_game(struct client *c)
{
	// TODO game
	move(0, 0);
	printw("game should be here");
	return RES_OK;
}

int render_game_quit(struct client *c)
{
	render_dialog("Quit",
			"Do you really want to quit the game?",
			GAME_QUIT,
			GAME_QUIT_LEN,
			c->data.game_quit.index);
	return RES_OK;
}

int render_halted(struct client *c)
{
	char *choices[] = {"BACK TO MENU"};
	render_dialog("Error", "Other player quit the game", (const char **)choices, 1, 0);
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
	case STATE_START_WAIT:
		return render_start_wait(c);
	case STATE_GAME:
		return render_game(c);
	case STATE_GAME_QUIT:
		return render_game_quit(c);
	case STATE_HALTED:
		return render_halted(c);
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

int client_redraw(struct client *c)
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
