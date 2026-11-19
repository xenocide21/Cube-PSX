/* host test: Cube's carrots, doors (tagged areas), teleports, jump pads and level end */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int count_tag(int tag, int type) {
	int x, y, n = 0;
	for (y = 0; y < world_ssize(); y++) for (x = 0; x < world_ssize(); x++) { const Cell *c = world_cell(x, y); if (c->tag == tag && c->type == type) n++; }
	return n;
}
static int mip_type0(int x, int y) { return wmip[0][y * world_ssize() + x].type; }

int main(void) {
	int m, i;
	for (m = 0; m < NUM_MAPS; m++) {
		game_start(m);
		printf("map %-8s triggers %d\n", map_names[m], ntrigs);
		for (i = 1; i < 256; i++) CHECK(count_tag(i, SPACE_T) == 0, "%s: tag %d not closed at start", map_names[m], i);
		for (i = 0; i < ntrigs; i++) {
			Trig *t = &trigs[i]; int k;
			if (t->type == E_CARROT) {
				int closed = count_tag(t->attr1, SOLID_T);
				game_start(m); t = &trigs[i];
				pl.x = t->x; pl.y = t->y; pl.z = t->z + 819; pl.vz = 0;
				game_tick(0, 0, 0, 0, 0, 0);
				CHECK(!t->spawned, "%s: carrot %d not picked up", map_names[m], i);
				CHECK(count_tag(t->attr1, SOLID_T) == 0, "%s: carrot tag %d left cells closed", map_names[m], t->attr1);
				for (k = 0; k < world_ssize() * world_ssize(); k++) if (world_cell(k % world_ssize(), k / world_ssize())->tag == t->attr1) CHECK(mip_type0(k % world_ssize(), k / world_ssize()) == SPACE_T, "renderer copy not updated");
				printf("  carrot tag %3d type %d: opened %d cells%s\n", t->attr1, t->attr2, closed, level_done ? "  -> level ends" : "");
				CHECK(((t->attr4 & 1) || t->attr2 == 2) == (level_done != 0), "%s: carrot tag %d level end wrong", map_names[m], t->attr1);
			} else if (t->type == E_TELEPORT) {
				int dx, dy, tx = -1, ty = -1;
				game_start(m); t = &trigs[i];
				for (k = 0; k < world_nents(); k++) { const MapEnt *d = world_ent(k); if (d->type == E_TELEDEST && d->attr2 == t->attr1) { tx = d->x; ty = d->y; } }
				pl.x = t->x + 300; pl.y = t->y; pl.z = t->z + 819;
				game_tick(0, 0, 0, 0, 0, 0);
				dx = (pl.x >> 8) - tx; dy = (pl.y >> 8) - ty;
				printf("  teleport tag %d -> (%d,%d)  player now at (%d,%d)\n", t->attr1, tx, ty, pl.x >> 8, pl.y >> 8);
				CHECK(tx >= 0 && iabs(dx) <= 2 && iabs(dy) <= 2, "%s: teleport %d did not arrive", map_names[m], t->attr1);
				for (k = 0; k < 60; k++) game_tick(0, 0, 0, 0, 0, 0);
				CHECK(!pl.dead, "player died after teleport");
			}
		}
	}
	/* jump pad (none in these maps): synthesise one under the player once it stands still */
	game_start(0);
	for (i = 0; i < 30; i++) game_tick(0, 0, 0, 0, 0, 0);
	{
		int z0 = pl.z, x0 = pl.x, top = 0, airtime = 0;
		Trig *t = &trigs[ntrigs++];
		t->x = pl.x; t->y = pl.y; t->z = pl.z - 819; t->type = E_JUMPPAD; t->attr1 = 30; t->attr2 = 0; t->attr3 = 20; t->spawned = 1;
		for (i = 0; i < 120; i++) { game_tick(0, 0, 0, 0, 0, 0); if (pl.z - z0 > top) top = pl.z - z0; if (!pl.onfloor) airtime++; }
		printf("jump pad 30/0/20: rose %.2f units, pushed %.2f units along x, %d ticks in the air\n", top / 256.0, (pl.x - x0) / 256.0, airtime);
		CHECK(top > 512, "jump pad did not lift");
		CHECK(pl.x - x0 > 256, "jump pad did not push along x");
	}
	/* the intermission holds until the menu takes over (START there returns to the map selector) */
	game_start(1);
	for (i = 0; i < ntrigs; i++) if (trigs[i].attr2 == 2) { pl.x = trigs[i].x; pl.y = trigs[i].y; pl.z = trigs[i].z + 819; }
	game_tick(0, 0, 0, 0, 0, 0);
	CHECK(level_done == 1, "exit carrot did not end cruel01");
	game_tick(0, 0, 0, 0, 0, 0);
	CHECK(level_done == 1, "intermission did not hold");
	{ int x0 = pl.x; game_tick(PAD_UP, PAD_START, 0, 0, 0, 0); CHECK(level_done == 1 && pl.x == x0, "the world should stand still in the intermission"); }
	/* killing everything ends the level */
	game_start(0);
	for (i = 0; i < nmons; i++) { mons[i].alive = 0; pl.kills++; }
	game_tick(0, 0, 0, 0, 0, 0);
	CHECK(level_done == 2, "clearing the map did not end it");
	printf("%s\n", fails ? "RESULT: FAILURES" : "RESULT: all checks passed");
	return fails != 0;
}
