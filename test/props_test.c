/* host test: map props (Cube's mapmodels) - their list, and Cube's mmcollide: a tree stops you, a bridge is a floor */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#define U 256

static int map_index(const char *n) { int i; for (i = 0; i < NUM_MAPS; i++) if (!strcmp(map_names[i], n)) return i; return -1; }

int main(void) {
	static const char *maps[] = { "cruel02", "wsg1", "wsg2", "wsg3", "pig01" };
	static const int want[] = { 2, 24, 56, 41, 0 };
	int k, i;
	for (k = 0; k < 5; k++) {
		game_start(map_index(maps[k]));
		printf("%-8s %d props\n", maps[k], world_nmm);
		CHECK(world_nmm == want[k], "%s: %d props, expected %d", maps[k], world_nmm, want[k]);
	}
	/* a tree (slot 0, rad 1, 8 high): walking into it from open ground stops you a body's width away */
	game_start(map_index("wsg1"));
	for (i = 0; i < nmons; i++) mons[i].alive = 0;
	for (i = 0; i < world_nmm; i++) {
		const MapModel *mm = &world_mm[i]; int fz, cz, ok, d;
		if (mm->mdl != 0) continue;
		/* find a side of it with open, level ground */
		for (d = 0; d < 4; d++) {
			int sx = mm->x + (d == 0 ? 4 : d == 1 ? -4 : 0) * U, sy = mm->y + (d == 2 ? 4 : d == 3 ? -4 : 0) * U;
			int floor = world_cell(sx >> 8, sy >> 8)->floor * U;
			if (!world_fits(sx, sy, 80, floor, 384, &fz, &cz)) continue;
			ok = world_fits(mm->x + (sx - mm->x) / 8, mm->y + (sy - mm->y) / 8, 80, floor, 384, &fz, &cz);
			printf("tree at (%d,%d): free 4 units away, %s half a unit from its middle\n", mm->x / U, mm->y / U, ok ? "FREE (wrong)" : "blocked");
			CHECK(!ok, "a tree trunk does not block");
			break;
		}
		break;
	}
	/* a bridge (slot 1, rad 4, zoff 4, 3 high): from above it is a floor at its top; from below, a ceiling */
	for (i = 0; i < world_nmm; i++) {
		const MapModel *mm = &world_mm[i]; int fz = 0, cz = 0, top = mm->z + mm->h;
		if (mm->mdl != 1) continue;
		if (world_fits(mm->x, mm->y, 80, top, 384, &fz, &cz)) {
			printf("bridge at (%d,%d) z %d..%d: standing on it, floor %d (%s)\n", mm->x / U, mm->y / U, mm->z / U, top / U, fz / U, fz == top ? "its deck" : "?");
			CHECK(fz == top, "a bridge is not a floor");
		} else printf("bridge at (%d,%d): no room on its deck here\n", mm->x / U, mm->y / U);
		if (world_fits(mm->x, mm->y, 80, world_cell(mm->x >> 8, mm->y >> 8)->floor * U, 384, &fz, &cz))
			{ printf("   and under it the ceiling is %d (its underside %d)\n", cz / U, mm->z / U); CHECK(cz <= mm->z, "a bridge is not a ceiling from below"); }
		break;
	}
	printf("RESULT: %s\n", fails ? "FAILED" : "all checks passed");
	return fails != 0;
}
