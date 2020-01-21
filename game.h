#pragma once

enum {
	GAME_NONE = -1,
	GAME_BLUE = 0,
	GAME_RED = 1,
};

struct game {
	int **fields;
	int width;
	int height;

	int turn;
	int over;
	int winner;
};

int game_init(struct game *g, int width, int height);

void game_finalize(struct game *g);

int game_drop(struct game *g, int color, int x);
