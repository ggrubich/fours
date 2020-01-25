#include <stdlib.h>

#include "game.h"

const int LINE_LENGTH = 4;
const int UNDOS = 3;

int game_init(struct game *g, int width, int height)
{
	int x, y;
	g->fields = malloc(width * sizeof(*g->fields));
	g->width = width;
	g->height = height;
	g->turn = SIDE_RED;
	g->over = 0;
	g->winner = SIDE_NONE;
	g->red_undos = UNDOS;
	g->blue_undos = UNDOS;
	g->last_x = -1;
	g->last_y = -1;
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

static int count_equal(struct game *g, int startx, int starty, int dx, int dy)
{
	int x, y;
	int count = 0;
	x = startx + dx;
	y = starty + dy;
	while (x >= 0 && x < g->width && y >= 0 && y < g->height) {
		if (g->fields[x][y] != g->fields[startx][starty]) {
			break;
		}
		++count;
		x += dx;
		y += dy;
	}
	return count;

}

static int is_line(struct game *g, int startx, int starty, int dx, int dy)
{
	int len = 1 + count_equal(g, startx, starty, -dx, -dy)
		+ count_equal(g, startx, starty, dx, dy);
	return len >= LINE_LENGTH;
}

static int is_connected(struct game *g, int startx, int starty)
{
	return is_line(g, startx, starty, 1, 0)
		|| is_line(g, startx, starty, 0, 1)
		|| is_line(g, startx, starty, 1, 1)
		|| is_line(g, startx, starty, 1, -1);
}

static int is_full(struct game *g)
{
	int i;
	for (i = 0; i < g->width; ++i) {
		if (g->fields[i][g->height - 1] == SIDE_NONE) {
			return 0;
		}
	}
	return 1;
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
	} else if (is_full(g)) {
		g->over = 1;
		g->winner = SIDE_NONE;
	}
	g->last_x = x;
	g->last_y = y;
	return y;
}

int game_undo(struct game *g, enum side side, int *x, int *y)
{
	if (g->over || g->turn == side || g->last_x < 0 || g->last_y < 0) {
		return -1;
	}
	if ((side == SIDE_RED && g->red_undos == 0)
			|| (side == SIDE_BLUE && g->blue_undos == 0))
	{
		return -1;
	}
	g->fields[g->last_x][g->last_y] = SIDE_NONE;
	*x = g->last_x;
	*y = g->last_y;
	g->last_x = -1;
	g->last_y = -1;
	if (side == SIDE_RED) {
		--g->red_undos;
	} else {
		--g->blue_undos;
	}
	g->turn = side;
	return 0;
}
