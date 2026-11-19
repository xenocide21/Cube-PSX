/* host test: the attract demo walks its whole route on cruel01, kills things, never dies, and stays inside the map */
#include "cube.h"
#include <stdio.h>
#include <math.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
int main(void) {
	int t = 0, stuck = 0, lastx, lasty, bad = 0;
	demo_begin(); lastx = pl.x; lasty = pl.y;
	while (!demo_done && t < 60 * 300) {
		const Cell *c;
		game_tick(0, 0, 0, 0, 0, 0); t++;
		c = world_cell(pl.x >> 8, pl.y >> 8);
		if (c->type == SOLID_T) bad++;
		if (t % 60 == 0) { if (pl.x == lastx && pl.y == lasty) stuck++; lastx = pl.x; lasty = pl.y; }
	}
	printf("demo: %d ticks (%.0f s), kills %d of %d, health %d, ticks inside solid cells %d, seconds standing still %d\n", t, t / 60.0, pl.kills, total_monsters, pl.health, bad, stuck);
	printf("%s\n", demo_done && !pl.dead && bad == 0 && pl.kills > 0 ? "RESULT: all checks passed" : "RESULT: FAILURES");
	return 0;
}
