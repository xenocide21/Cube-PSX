/* host test: the XENO pack - every campaign map loads, each carrot can be stood on and opens its doors, in the order
   the map is meant to be played, and the exit carrot ends the level */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
extern int level_done;
static int map_index(const char *n) { int i; for (i = 0; i < NUM_MAPS; i++) if (!strcmp(map_names[i], n)) return i; return -1; }
int main(void) {
	static const char *maps[] = { "xeno01", "xeno02", "xeno03", "xeno04", "xeno05", "xeno06", "xeno07", "xeno08", "xdm1", "xdm2", "xdm3" };
	int k;
	for (k = 0; k < 11; k++) {
		int mi = map_index(maps[k]), i, t, ncar = 0, opened = 0, ended = 0, fz, cz;
		CHECK(mi >= 0, "%s is not in the map list", maps[k]);
		if (mi < 0) continue;
		game_start(mi);
		CHECK(world_fits(pl.x, pl.y, 281, pl.z - 819, 384, &fz, &cz), "%s: the start is blocked", maps[k]);
		/* carrots in tag order, the exit (50) last */
		for (t = 1; t <= 50; t++)
			for (i = 0; i < world_nents(); i++) {
				const MapEnt *e = world_ent(i); int x, y, before = 0, after = 0, c;
				if (e->type != E_CARROT || e->attr1 != t) continue;
				ncar++;
				for (c = 0; c < world_ssize() * world_ssize(); c++) if (world_cell(c % world_ssize(), c / world_ssize())->tag == t && world_cell(c % world_ssize(), c / world_ssize())->type == SOLID_T) before++;
				x = e->x * 256 + 128; y = e->y * 256 + 128;
				CHECK(world_fits(x, y, 281, world_cell(e->x, e->y)->floor * 256, 384, &fz, &cz), "%s: no room to stand on carrot %d at (%d,%d)", maps[k], t, e->x, e->y);
				pl.x = x; pl.y = y; pl.z = world_cell(e->x, e->y)->floor * 256 + 819; pl.vz = 0; pl.health = 100; pl.dead = 0;
				game_tick(0, 0, 0, 0, 0, 0); game_tick(0, 0, 0, 0, 0, 0);
				for (c = 0; c < world_ssize() * world_ssize(); c++) if (world_cell(c % world_ssize(), c / world_ssize())->tag == t && world_cell(c % world_ssize(), c / world_ssize())->type == SOLID_T) after++;
				if (before) { CHECK(after == 0, "%s: carrot %d left %d of its %d door cells shut", maps[k], t, after, before); opened += before; }
				if (t == 50) ended = level_done;
			}
		if (k < 8) CHECK(ended, "%s: the exit carrot did not end the level", maps[k]);
		printf("%-7s \"%s\" %dx%d: %d monsters, %d carrots, %d door cells opened, %s\n", maps[k], world_title(), world_ssize(), world_ssize(),
		       total_monsters, ncar, opened, k < 8 ? (ended ? "exit ends the level" : "NO EXIT") : "(multiplayer)");
	}
	printf("RESULT: %s\n", fails ? "FAILED" : "all checks passed");
	return fails != 0;
}
