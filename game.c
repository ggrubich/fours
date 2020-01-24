#include <stdlib.h>

#include "game.h"

const int LINE_LENGTH = 4;

int game_init(struct game *g, int width, int height)
{
	int x, y;
	g->fields = malloc(width * sizeof(*g->fields));
	g->width = width;
	g->height = height;
	g->turn = SIDE_RED;
	g->over = 0;
	g->winner = SIDE_NONE;
	if (!g->fields) {
		return -1;
	}
	for (x = 0; x < width; ++x) {
		g->fields[x] = malloc(height * sizeof(**g->fields));
		if (!g->fields[x]) {
			while (x > 0) {
				free(g->fields[x-1]);
				--x;
			}
			free(g->fields);
			return -1;
		}
		for (y = 0; y < height; ++y) {
			g->fields[x][y] = SIDE_NONE;
		}
	}
	return 0;
}

void game_finalize(struct game *g)
{
	size_t i;
	for (i = 0; i < g->width; ++i) {
		free(g->fields[i]);
	}
	free(g->fields);
}

static int line_equal(struct game *g, int startx, int starty, int dx, int dy)
{
	int endx, endy;
	int x, y;
	endx = startx + (LINE_LENGTH - 1) * dx;
	endy = starty + (LINE_LENGTH - 1) * dy;
	if (endx < 0 || endx >= g->width) {
		return 0;
	}
	if (endy < 0 || endy >= g->height) {
		return 0;
	}
	x = startx;
	y = starty;
	while (x != endx || y != endy) {
		if (g->fields[x][y] != g->fields[startx][starty]) {
			return 0;
		}
		x += dx;
		y += dy;
	}
	return 1;
}

static int is_connected(struct game *g, int x, int y)
{
	int dx, dy;
	if (line_equal(g, x, y, 0, -1)) {
		return 1;
	}
	for (dx = -1; dx <= 1; dx += 2) {
		for (dy = -1; dy <= 1; ++dy) {
			if (line_equal(g, x, y, dx, dy)) {
				return 1;
			}
		}
	}
	return 0;
}

int game_drop(struct game *g, enum side side, int x)
{
	int y;
	if (g->over || g->turn != side || x < 0 || x >= g->width) {
		return -1;
	}
	for (y = 0; y < g->height; ++y) {
		if (g->fields[x][y] == SIDE_NONE) {
			break;
		}
	}
	if (y >= g->height) {
		return -1;
	}
	g->fields[x][y] = side;
	g->turn = side == SIDE_RED ? SIDE_BLUE : SIDE_RED;
	if (is_connected(g, x, y)) {
		g->over = 1;
		g->winner = side;
	}
	return y;
}
