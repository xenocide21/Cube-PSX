/* host test: water (physics.cpp moveplayer) on pig09, whose spawn is a flooded canal you have to swim out of */
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

static void quiet(void) { int i; for (i = 0; i < nmons; i++) mons[i].alive = 0; }
static int feet(void) { return pl.z - 819; }

int main(void) {
	int mi, k, t, best = -1, bx = 0, by = 0, escaped = 0;
	for (mi = 0; mi < NUM_MAPS; mi++) if (!strcmp(map_names[mi], "pig09")) break;
	game_start(mi); quiet();
	printf("pig09: water level %d, spawn at (%d,%d) floor %d, eye %.1f units\n", world_water(), pl.x / U, pl.y / U,
		world_cell(pl.x / U, pl.y / U)->floor, pl.z / 256.0);
	CHECK(world_water() == 0, "pig09 water level not loaded");

	/* standing still under water you sink to the bottom, slowly */
	pl.z += 3 * U;
	for (k = 0; k < 60; k++) game_tick(0, 0, 0, 0, 0, 0);
	{ double h = (feet() - world_cell(pl.x / U, pl.y / U)->floor * U) / 256.0;
	  printf("sinking: from 3 units above the bottom, after 1 s %.2f above (Cube sinks 2.5 units a second)\n", h);
	  CHECK(pl.inwater, "not in water at the pig09 spawn");
	  CHECK(h > 0.2 && h < 1.0, "sinking speed is not Cube's"); }
	for (k = 0; k < 600; k++) game_tick(0, 0, 0, 0, 0, 0);
	CHECK(pl.onfloor, "never settled on the bottom");

	/* swimming: looking up and pushing forward takes you up */
	{
		int z0 = pl.z;
		pl.pitch = -700;
		for (k = 0; k < 60; k++) game_tick(PAD_UP, 0, 0, 0, 0, 0);
		printf("swimming: looking up, 1 s forward rose %.2f units\n", (pl.z - z0) / 256.0);
		CHECK(pl.z - z0 > U, "looking up and swimming forward did not rise");
	}

	/* the way out: follow the canal to where there is headroom and a ramp up to the field, stroke up (X) and climb out */
	{
		static int prev[256 * 256]; static int q[256 * 256]; int n = world_ssize(), qh = 0, qt = 0, goal = -1, path[512], np = 0, c, wp = 0;
		memset(prev, -1, sizeof(prev));
		game_start(mi); quiet();
		c = (pl.y / U) * n + pl.x / U; prev[c] = c; q[qt++] = c;
		while (qh < qt && goal < 0) {
			int cur = q[qh++], x = cur % n, y = cur / n, d;
			static const int ox[4] = { 1, -1, 0, 0 }, oy[4] = { 0, 0, 1, -1 };
			for (d = 0; d < 4; d++) {
				int nx = x + ox[d], ny = y + oy[d], nc = ny * n + nx; const Cell *e = world_cell(nx, ny);
				if (prev[nc] >= 0 || e->type == SOLID_T || e->type == CORNER_T || e->ceil - e->floor < 4 || e->tag) continue;
				if (e->type == FHF_T && e->ceil > 0) { prev[nc] = cur; goal = nc; break; }      /* the ramp */
				if (e->floor > -7) continue;                                                     /* stay in the canal */
				prev[nc] = cur; q[qt++] = nc;
			}
		}
		CHECK(goal >= 0, "no ramp out of the canal found");
		for (c = goal; c != prev[c] && np < 512; c = prev[c]) path[np++] = c;
		bx = goal % n; by = goal / n;
		printf("way out: ramp at cell (%d,%d), %d cells along the canal\n", bx, by, np);
		/* the canal is proven connected to the ramp (the search above swam there cell by cell); start at its foot */
		{
			int k2, fx = bx, fy = by, fz2, cz2, found = 0, sx, sy;
			for (k2 = 0; k2 < 49 && !found; k2++)                 /* a legal spot on the canal bottom, under headroom, near the opening */
				for (sy = -3; sy <= 3 && !found; sy++) {
					int px = (bx + k2 % 7 - 3) * U + 128 + (k2 / 7 - 3) * 64, py = (by + sy) * U + 128;
					if (world_cell(px >> 8, py >> 8)->floor > -8) continue;
					if (world_fits(px, py, 281, -8 * U, 384, &fz2, &cz2) && cz2 > 0 && fz2 == -8 * U) { found = 1; fx = px; fy = py; }
				}
			CHECK(found, "no legal start spot near the opening");
			pl.x = fx; pl.y = fy; pl.z = -8 * U + 819; pl.vz = 0;
			printf("start at the bottom of the canal at (%.1f,%.1f) under the opening (ceiling %d)\n", fx / 256.0, fy / 256.0, cz2 / U);
		}
		for (t = 0; t < 60 * 20 && !escaped; t++) {
			pl.yaw = 0;                                       /* straight at the field (+y) */
			pl.pitch = -500;                                  /* look up a little and swim at the bank, stroking up */
			game_tick(PAD_UP, t % 15 == 0 ? PAD_CROSS : 0, 0, 0, 0, 0);
			if (getenv("TRACE") && t % 10 == 0) printf("t%d pos %.1f,%.1f feet %.2f vz %d water %d floor %d onfloor %d\n", t, pl.x/256.0, pl.y/256.0, (pl.z-819)/256.0, pl.vz, pl.inwater, world_cell(pl.x / U, pl.y / U)->floor, pl.onfloor);
			if (!pl.inwater && pl.onfloor && world_cell(pl.x / U, pl.y / U)->floor >= -1) escaped = t ? t : 1;
		}
		printf("escape: %s after %.1f s (feet at %.2f, in water %d)\n", escaped ? "out of the canal, on the ramp" : "still in the canal", escaped / 60.0, feet() / 256.0, pl.inwater);
		CHECK(escaped, "cannot get out of the pig09 canal");
		for (t = 0; t < 120 && escaped; t++) { int dx = bx * U + 128 - pl.x, dy = by * U + 128 - pl.y; (void)dx; (void)dy; game_tick(PAD_UP, 0, 0, 0, 0, 0); }
		printf("        walked on: feet at %.2f, cell floor %d\n", feet() / 256.0, world_cell(pl.x / U, pl.y / U)->floor);
	}

	/* and back on land, normal physics */
	{
		int z0;
		for (k = 0; k < 30; k++) game_tick(0, 0, 0, 0, 0, 0);
		z0 = pl.z;
		game_tick(0, PAD_CROSS, 0, 0, 0, 0);
		for (k = 0; k < 10; k++) game_tick(0, 0, 0, 0, 0, 0);
		CHECK(!pl.inwater && pl.z > z0, "no normal jump on land");
	}
	printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: all checks passed\n", fails);
	return fails != 0;
}
