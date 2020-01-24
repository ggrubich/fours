#include <string.h>
#include <ncurses.h>

#include "client_render.h"

const char *LOBBY[] = {"START", "QUIT"};

const char *GAME_QUIT[] = {"YES", "NO"};

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

const int DIALOG_PAD = 2;

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
	// TODO game
	move(0, 0);
	printw("game should be here");
	return RES_OK;
}

static int render_game_quit(struct client *c)
{
	render_dialog("Quit",
			"Do you really want to quit the game?",
			GAME_QUIT,
			GAME_QUIT_LEN,
			c->data.game_quit.index);
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
	case STATE_HALTED:
		return render_halted(c);
	}
	return RES_OK;
}
