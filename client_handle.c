#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

#include "client_handle.h"

#define KEY_ESC 27

static int goto_lobby(struct client *c)
{
	c->state = STATE_LOBBY;
	c->data.lobby.index = 0;
	return RES_OK;
}

static int handle_login_wait(struct client *c, struct event *ev)
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

static int handle_login_err(struct client *c, struct event *ev)
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

static int handle_lobby(struct client *c, struct event *ev)
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

static int handle_start_wait(struct client *c, struct event *ev)
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

static int handle_game(struct client *c, struct event *ev)
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

static int handle_game_quit(struct client *c, struct event *ev)
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

static int handle_halted(struct client *c, struct event *ev)
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
