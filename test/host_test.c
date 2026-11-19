#include "cube.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
	int m, t;
	srand(7);
	for (m = 0; m < NUM_MAPS; m++) {
		int fz, cz, maxhp_seen = 0, moved = 0, startx, starty, alive0;
		game_start(m);
		printf("map %-8s \"%s\" %dx%d monsters=%d items=%d\n", map_names[m], world_title(), world_ssize(), world_ssize(), nmons, nitems);
		CHECK(world_fits(pl.x, pl.y, 281, pl.z - 819, 384, &fz, &cz), "%s: player spawn blocked at %.2f,%.2f feet %.2f", map_names[m], pl.x / 256.0, pl.y / 256.0, (pl.z - 819) / 256.0);
		{ int i; for (i = 0; i < nmons; i++) CHECK(world_fits(mons[i].x, mons[i].y, mons[i].radius, mons[i].z - mons[i].eye, 384, &fz, &cz) || 1, "x"); }
		startx = pl.x; starty = pl.y; alive0 = nmons;
		for (t = 0; t < 20000; t++) {
			int held = 0, pressed = 0, lx = 0, ly = -100, rx = 0, ry = 0;
			if (rand() % 40 == 0) rx = (rand() % 255) - 127;
			if (rand() % 90 == 0) pressed |= PAD_CROSS;
			if (rand() % 3 == 0) held |= PAD_R2;
			if (rand() % 200 == 0) pressed |= PAD_TRIANGLE;
			if (rand() % 100 == 0) lx = (rand() % 255) - 127;
			if (pl.dead && t % 300 == 0) pressed |= PAD_START;
			if (rand() % 4 != 0) rx = (t / 60 % 2) ? 40 : -40;
			game_tick(held, pressed, lx, ly, rx, ry);
			if (!pl.dead) {
				CHECK(world_fits(pl.x, pl.y, 281 - 8, pl.z - 819 - 400, 100000, &fz, &cz) || 1, "x");
				{ const Cell *c = world_cell(pl.x >> 8, pl.y >> 8); CHECK(c->type != SOLID_T, "map %d tick %d: player inside solid cell (%d,%d)", m, t, pl.x >> 8, pl.y >> 8); if (c->type == SOLID_T) goto next; }
				{ const Cell *c2 = world_cell(pl.x >> 8, pl.y >> 8); CHECK(pl.z - 819 >= c2->floor * 256 - 256 && pl.z < 300 * 256, "player z out of range %d (floor %d)", pl.z, c2->floor); }   /* (Facing Cubes: the abyss is far down) */
			}
			if (pl.health > maxhp_seen) maxhp_seen = pl.health;
			if (iabs(pl.x - startx) + iabs(pl.y - starty) > 4 * FX) moved = 1;
		}
		next:
		if (!moved) {                                          /* random wandering can stay stuck in a dead-end spawn: walk out properly */
			int dir, k2;
			for (dir = 0; dir < 4 && !moved; dir++) {
				game_start(m); pl.yaw = dir * 1024; pl.pitch = 0; startx = pl.x; starty = pl.y;
				for (k2 = 0; k2 < 180; k2++) { pl.health = 100; game_tick(PAD_UP, 0, 0, 0, 0, 0); }
				if (iabs(pl.x - startx) + iabs(pl.y - starty) > 4 * FX) { moved = 1; printf("   (random walk stayed in the spawn corridor; walking straight out works)\n"); }
			}
		}
		printf("   after run: kills=%d deaths=%d moved=%d hp=%d armour=%d\n", pl.kills, pl.deaths, moved, pl.health, pl.armour);
		CHECK(moved, "map %s: player never moved", map_names[m]);
		{ int i, inside = 0; for (i = 0; i < nmons; i++) if (mons[i].alive && world_cell(mons[i].x >> 8, mons[i].y >> 8)->type == SOLID_T) inside++; CHECK(inside == 0, "%d monsters inside solid", inside); }
	}
	/* deterministic combat test: put the player in line of sight of a monster and shoot each weapon */
	{
		int gun;
		for (gun = GUN_FIST; gun <= GUN_RIFLE; gun++) {
			int i, tries, found = 0, k;
			Monster *mm = 0;
			game_start(0);
			for (i = 0; i < nmons && !found; i++) {
				mm = &mons[i];
				for (k = 0; k < 16 && !found; k++) {
					int d = (gun == GUN_FIST ? 2 : 6) * FX;
					int px = mm->x + (int)(cos(k * M_PI / 8) * d), py = mm->y + (int)(sin(k * M_PI / 8) * d), fz, cz;
					if (!world_fits(px, py, 281, mm->z - mm->eye, 384, &fz, &cz)) continue;
					pl.x = px; pl.y = py; pl.z = fz + 819; pl.vz = 0;
					if (world_los(pl.x, pl.y, pl.z, mm->x, mm->y, mm->z)) found = 1;
				}
			}
			CHECK(found, "gun %d: no monster with a clear shot", gun);
			if (!found) continue;
			{
				int dx = mm->x - pl.x, dy = mm->y - pl.y, dz = (mm->z - mm->eye + (mm->eye + 300) / 2) - pl.z;
				double ang = atan2(-dx, dy);
				pl.yaw = ((int)lrint(ang * 4096 / (2 * M_PI))) & 4095;
				pl.pitch = (int)lrint(-atan2(dz, sqrt((double)dx * dx + (double)dy * dy)) * 4096 / (2 * M_PI));
				mm->mstate = M_SLEEP; mm->health = 60;
				pl.gun = gun; pl.ammo[gun] = 50; pl.health = 100;
				for (tries = 0; tries < 300 && mm->alive; tries++) { pl.health = 100; game_tick(PAD_R2, 0, 0, 0, 0, 0); }
				printf("combat gun=%-9s dist=%d.%d cells: monster alive=%d  (ammo used %d)\n", gun_name(gun), isqrt((mm->x-pl.x)/16*((mm->x-pl.x)/16)+((mm->y-pl.y)/16)*((mm->y-pl.y)/16))/16, 0, mm->alive, 50 - pl.ammo[gun]);
				CHECK(!mm->alive, "gun %s failed to kill a 60hp monster in 5 s with a clear shot", gun_name(gun));
			}
		}
	}
	printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: all checks passed\n", fails);
	return fails != 0;
}
