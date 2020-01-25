#include <string.h>
#include <ncurses.h>

#include "client_render.h"

const char *LOBBY[] = {"START", "QUIT"};

const char *GAME_QUIT[] = {"YES", "NO"};

#define RED_CELL_PAIR 1
#define BLUE_CELL_PAIR 2
#define HOOK_PAIR 3
#define RED_TEXT_PAIR 4
#define BLUE_TEXT_PAIR 5

void render_init(void)
{
	start_color();
	use_default_colors();
	init_pair(RED_CELL_PAIR, -1, COLOR_RED);
	init_pair(BLUE_CELL_PAIR, -1, COLOR_BLUE);
	init_pair(HOOK_PAIR, -1, COLOR_WHITE);
	init_pair(RED_TEXT_PAIR, COLOR_RED, -1);
	init_pair(BLUE_TEXT_PAIR, COLOR_BLUE, -1);
}

static void padded(const char *text, int left, int right)
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

static void centered(const char *text, int width)
{
	int pad = width - strlen(text);
	int left = pad / 2;
	int right = pad - left;
	padded(text, left, right);
}

const int MENU_PAD = 6;

static void render_menu(const char **items, int len, int current)
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

static void frame(int width, int height)
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

static void mvfill(int starty, int startx, int height, int width, int ch)
{
	int i, j;
	for (i = 0; i < height; ++i) {
		for (j = 0; j < width; ++j) {
			mvaddch(starty + i, startx + j, ch);
		}
	}
}

const int DIALOG_PAD = 2;

static int dialog_height(int len)
{
	return 7 + len;
}

static int dialog_width(const char *title, const char *text, const char **choices, int len)
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

static void render_dialog_above(int bottom_space, const char *title, const char *text,
		const char **choices, int len, int current)
{
	int i;
	int scr_height, scr_width;
	int height, width;
	int starty, startx;
	getmaxyx(stdscr, scr_height, scr_width);
	height = dialog_height(len);
	width = dialog_width(title, text, choices, len);
	starty = ((scr_height - bottom_space) - height) / 2;
	startx = (scr_width - width) / 2;
	mvfill(starty, startx, height, width, ' ');
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

static void render_dialog(const char *title, const char *text,
		const char **choices, int len, int current)
{
	render_dialog_above(0, title, text, choices, len, current);
}

const int CELL_HEIGHT = 3;
const int CELL_WIDTH = 6;

static int gridy(int i)
{
	return i * (CELL_HEIGHT + 1);
}

static int gridx(int i)
{
	return i * (CELL_WIDTH + 1);
}

static int grid_height(int h)
{
	return 1 + gridy(h);
}

static int grid_width(int w)
{
	return 1 + gridx(w);
}

static void mvgrid(int starty, int startx, int height, int width)
{
	int i, j;
	// lines
	for (i = 0; i <= height; ++i) {
		mvhline(starty + gridy(i), startx, 0, grid_width(width));
	}
	for (i = 0; i <= width; ++i) {
		mvvline(starty, startx + gridx(i), 0, grid_height(height));
	}
	// corners
	mvaddch(starty, startx, ACS_ULCORNER);
	mvaddch(starty, startx + gridx(width), ACS_URCORNER);
	mvaddch(starty + gridy(height), startx, ACS_LLCORNER);
	mvaddch(starty + gridy(height), startx + gridx(width), ACS_LRCORNER);
	// borders
	for (i = 1; i < width; ++i) {
		mvaddch(starty, startx + gridx(i), ACS_TTEE);
	}
	for (i = 1; i < width; ++i) {
		mvaddch(starty + gridy(height), startx + gridx(i), ACS_BTEE);
	}
	for (i = 1; i < height; ++i) {
		mvaddch(starty + gridy(i), startx, ACS_LTEE);
	}
	for (i = 1; i < height; ++i) {
		mvaddch(starty + gridy(i), startx + gridx(width), ACS_RTEE);
	}
	// crosses
	for (i = 1; i < height; ++i) {
		for (j = 1; j < width; ++j) {
			mvaddch(starty + gridy(i), startx + gridx(j), ACS_PLUS);
		}
	}
}

static void mvboard(int starty, int startx, enum side **board, int height, int width)
{
	int i, j;
	int sym;
	mvgrid(starty, startx, height, width);
	for (i = 0; i < height; ++i) {
		for (j = 0; j < width; ++j) {
			switch (board[j][height - 1 - i]) {
			case SIDE_RED:
				sym = ' ' | COLOR_PAIR(RED_CELL_PAIR);
				break;
			case SIDE_BLUE:
				sym = ' ' | COLOR_PAIR(BLUE_CELL_PAIR);
				break;
			default:
				sym = ' ';
			}
			mvfill(starty + gridy(i) + 1,
				startx + gridx(j) + 1,
				CELL_HEIGHT,
				CELL_WIDTH,
				sym);
		}
	}
}

const int HOOK_HEIGHT = 1;

static void render_board(enum side **board, int height, int width,
		int column, enum side side)
{
	int scr_height, scr_width;
	int starty, startx;
	getmaxyx(stdscr, scr_height, scr_width);
	starty = (scr_height - grid_height(height) - 1);
	startx = (scr_width - grid_width(width)) / 2;
	mvboard(starty, startx, board, height, width);
	mvfill(starty - CELL_HEIGHT - HOOK_HEIGHT,
		startx + gridx(column) + 1,
		HOOK_HEIGHT,
		CELL_WIDTH,
		' ' | COLOR_PAIR(HOOK_PAIR));
	mvfill(starty - CELL_HEIGHT,
		startx + gridx(column) + 1,
		CELL_HEIGHT,
		CELL_WIDTH,
		' ' | COLOR_PAIR(side == SIDE_RED ? RED_CELL_PAIR : BLUE_CELL_PAIR));
}

static void left_sticking(int y, int gapx, char *str)
{
	int scr_width = getmaxx(stdscr);
	int max = (scr_width - (2 * gapx)) / 2;
	mvaddnstr(y, gapx, str, max);
}

static void right_sticking(int y, int gapx, char *str)
{
	int scr_width = getmaxx(stdscr);
	int max = (scr_width - (2 * gapx)) / 2;
	if (strlen(str) <= max) {
		mvaddstr(y, scr_width - gapx - strlen(str), str);
	} else {
		mvaddnstr(y, scr_width - gapx - max, str, max);
	}
}

const int PLAYER_PAD = 1;

static void render_players(struct client *c)
{
	int left_attr, right_attr;
	char left_buf[32], right_buf[32];
	if (c->data.game.b.side == SIDE_RED) {
		left_attr = A_BOLD | COLOR_PAIR(RED_TEXT_PAIR);
		right_attr = A_BOLD | COLOR_PAIR(BLUE_TEXT_PAIR);
		sprintf(left_buf, "undos: %d", c->data.game.b.red_undos);
		sprintf(right_buf, "undos: %d", c->data.game.b.blue_undos);
	} else {
		left_attr = A_BOLD | COLOR_PAIR(BLUE_TEXT_PAIR);
		right_attr = A_BOLD | COLOR_PAIR(RED_TEXT_PAIR);
		sprintf(left_buf, "undos: %d", c->data.game.b.blue_undos);
		sprintf(right_buf, "undos: %d", c->data.game.b.red_undos);
	}

	attron(left_attr);
	left_sticking(PLAYER_PAD, PLAYER_PAD, c->name);
	left_sticking(PLAYER_PAD + 1, PLAYER_PAD, left_buf);
	attroff(left_attr);

	attron(right_attr);
	right_sticking(PLAYER_PAD, PLAYER_PAD, c->data.game.b.other);
	right_sticking(PLAYER_PAD + 1, PLAYER_PAD, right_buf);
	attroff(right_attr);
}

static void render_base_game(struct client *c)
{
	render_board(
		c->data.game.b.board,
		c->data.game.b.height,
		c->data.game.b.width,
		c->data.game.b.column,
		c->data.game.b.side);
	render_players(c);
}

const int MESSAGE_OFFSET = 4;

static void render_game_message(char *msg)
{
	move(MESSAGE_OFFSET, 0);
	attron(A_BOLD);
	centered(msg, getmaxx(stdscr));
	attroff(A_BOLD);
}

// functions for each state

static int render_login_wait(struct client *c)
{
	move(0, 0);
	printw("logging in...\n");
	return RES_OK;
}

static int render_login_err(struct client *c)
{
	char *choices[] = {"OK"};
	render_dialog("Error", c->data.login_err, (const char **)choices, 1, 0);
	return RES_OK;
}

static int render_lobby(struct client *c)
{
	render_menu(LOBBY, LOBBY_LEN, c->data.lobby.index);
	return RES_OK;
}

static int render_start_wait(struct client *c)
{
	char *choices[] = {"CANCEL"};
	render_dialog("Starting...",
			"Waiting for the second player to join",
			(const char **)choices,
			1,
			0);
	return RES_OK;
}

static int render_game(struct client *c)
{
	char *msg;
	render_base_game(c);
	if (c->data.game.b.side == c->data.game.b.turn) {
		msg = "Your turn";
	} else {
		msg = "Opponent's turn";
	}
	render_game_message(msg);
	return RES_OK;
}

static int render_game_quit(struct client *c)
{
	render_base_game(c);
	render_dialog("Quit",
			"Do you really want to quit the game?",
			GAME_QUIT,
			GAME_QUIT_LEN,
			c->data.game_quit.index);
	return RES_OK;
}

static int render_game_over(struct client *c)
{
	char *msg;
	char *choices[] = {"BACK TO MENU"};
	render_base_game(c);
	if (c->data.game_over.b.side == c->data.game_over.winner) {
		msg = "You win!";
	} else {
		msg = "You lose";
	}
	render_dialog_above(grid_height(c->data.game.b.height),
			"Game Over", msg, (const char **)choices, 1, 0);
	return RES_OK;
}

static int render_halted(struct client *c)
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
	case STATE_GAME_OVER:
		return render_game_over(c);
	case STATE_HALTED:
		return render_halted(c);
	}
	return RES_OK;
}
