#pragma once

#include "side.h"

struct game {
	enum side **fields;
	int width;
	int height;

	enum side turn;
	int over;
	enum side winner;
};

int game_init(struct game *g, int width, int height);

void game_finalize(struct game *g);

int game_drop(struct game *g, enum side side, int x);
