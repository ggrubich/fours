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

static enum side **create_board(int width, int height)
{
	int i, j;
	enum side **board;
	board = malloc(width * sizeof(*board));
	if (!board) {
		return NULL;
	}
	for (i = 0; i < width; ++i) {
		board[i] = malloc(height * sizeof(**board));
		if (!board[i]) {
			for (j = 0; j < i; ++j) {
				free(board[j]);
			}
			free(board);
			return NULL;
		}
		for (j = 0; j < height; ++j) {
			board[i][j] = SIDE_NONE;
		}
	}
	return board;
}

static int goto_game(struct client *c, struct message *msg)
{
	struct game_base *base = &c->data.game.b;
	c->state = STATE_GAME;
	base->other = strdup(msg->data.start_ok.other);
	if (!base->other) {
		return RES_ERR;
	}
	base->side = msg->data.start_ok.side;
	base->width = msg->data.start_ok.width;
	base->height = msg->data.start_ok.height;
	base->column = base->width / 2;
	base->turn = SIDE_RED;
	base->red_undos = msg->data.start_ok.red_undos;
	base->blue_undos = msg->data.start_ok.blue_undos;
	base->board = create_board(base->width, base->height);
	if (!base->board) {
		free(base->other);
		return RES_ERR;
	}
	return RES_OK;
}

static void move_cursor(int delta, int *idx, int len)
{
	int new_idx = *idx + delta;
	if (new_idx >= 0 && new_idx < len) {
		*idx = new_idx;
	}
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
			move_cursor(-1, &c->data.lobby.index, LOBBY_LEN);
			break;
		case KEY_DOWN:
			move_cursor(1, &c->data.lobby.index, LOBBY_LEN);
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
			return goto_game(c, msg);
		default:
			break;
		}
	}
	return RES_OK;
}

static int handle_game(struct client *c, struct event *ev)
{
	enum side side;
	int column, row;
	struct message req;
	struct message *msg;
	int res;
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_ESC:
			c->state = STATE_GAME_QUIT;
			c->data.game_quit.index = 0;
			break;
		case KEY_LEFT:
			move_cursor(-1, &c->data.game.b.column, c->data.game.b.width);
			break;
		case KEY_RIGHT:
			move_cursor(1, &c->data.game.b.column, c->data.game.b.width);
			break;
		case KEY_ENTER:
		case '\n':
			req.type = MSG_DROP;
			req.data.drop.column = c->data.game.b.column;
			if ((res = request(c, &req)) < 0) {
				return res;
			}
			break;
		case 'u':
			req.type = MSG_UNDO;
			if ((res = request(c, &req)) < 0) {
				return res;
			}
			break;
		default:
			break;
		}
	} else if (ev->type == EVENT_MSG) {
		msg = ev->data.msg;
		switch (msg->type) {
		case MSG_NOTIFY_QUIT:
			finalize_state(c);
			c->state = STATE_HALTED;
			break;
		case MSG_NOTIFY_OVER:
			c->state = STATE_GAME_OVER;
			c->data.game_over.winner = msg->data.notify_over.winner;
			break;
		case MSG_NOTIFY_DROP:
			column = msg->data.notify_drop.column;
			row = msg->data.notify_drop.row;
			c->data.game.b.board[column][row] = msg->data.notify_drop.side;
			c->data.game.b.turn = msg->data.notify_drop.side == SIDE_RED
				? SIDE_BLUE
				: SIDE_RED;
			break;
		case MSG_NOTIFY_UNDO:
			column = msg->data.notify_undo.column;
			row = msg->data.notify_undo.row;
			side = msg->data.notify_undo.side;
			c->data.game.b.board[column][row] = SIDE_NONE;
			c->data.game.b.turn = side;
			if (side == SIDE_RED) {
				--c->data.game.b.red_undos;
			} else {
				--c->data.game.b.blue_undos;
			}
			break;
		default:
			break;
		}
	}
	return RES_OK;
}

static int handle_game_quit(struct client *c, struct event *ev)
{
	struct message req;
	int res;
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_UP:
			move_cursor(-1, &c->data.game_quit.index, GAME_QUIT_LEN);
			break;
		case KEY_DOWN:
			move_cursor(1, &c->data.game_quit.index, GAME_QUIT_LEN);
			break;
		case KEY_ENTER:
		case '\n':
			if (c->data.game_quit.index == GAME_QUIT_YES) {
				req.type = MSG_QUIT;
				if ((res = request(c, &req)) < 0) {
					return res;
				}
				finalize_state(c);
				return goto_lobby(c);
			} else {
				c->state = STATE_GAME;
			}
			break;
		}
	} else if (ev->type == EVENT_MSG) {
		return handle_game(c, ev);
	}
	return RES_OK;
}

static int handle_game_over(struct client *c, struct event *ev)
{
	if (ev->type == EVENT_INPUT) {
		switch (ev->data.ch) {
		case KEY_ENTER:
		case '\n':
			finalize_state(c);
			return goto_lobby(c);
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
	case STATE_GAME_OVER:
		return handle_game_over(c, ev);
	case STATE_HALTED:
		return handle_halted(c, ev);
	}
	return RES_OK;
}
