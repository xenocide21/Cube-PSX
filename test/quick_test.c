/* host test: quickplay - bots find their way, fight, take flags and score; falling into space kills */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static int map_index(const char *n) { int i; for (i = 0; i < NUM_MAPS; i++) if (!strcmp(map_names[i], n)) return i; return -1; }

static int on_hill_test(int x, int y) { int hx, hy, hz, ho; koth_hill(&hx, &hy, &hz, &ho); return iabs(x - hx) < 8 * FX && iabs(y - hy) < 12 * FX; }
int main(void) {
	int t, i, fc = map_index("fcubes"), took = 0, upper = 0, kills = 0, fell = 0;
	/* the warm-up: the map is yours alone - no bots, nothing to pick up, a fall puts you back - until fire */
	{
		int alive = 0, h0, sx, sy;
		game_start_quick(fc, GM_CTF, 4);
		CHECK(qp_waiting, "no warm-up after loading");
		for (t = 0; t < 120; t++) game_tick(PAD_UP, 0, 0, 0, 0, 0);
		for (i = 0; i < nmons; i++) alive += mons[i].alive;
		CHECK(alive == 0, "%d bots about in the warm-up", alive);
		h0 = 0; for (i = 0; i < nitems; i++) h0 += items[i].taken;
		CHECK(h0 == 0, "an item was picked up in the warm-up");
		{   /* no-clip: hold forward and you go through walls; look up and you climb; X rises */
			int z0 = pl.z, k, solid = 0, z1;
			for (k = 0; k < 600; k++) { game_tick(PAD_UP, 0, 0, 0, 0, 0); if (world_cell(pl.x >> 8, pl.y >> 8)->type == SOLID_T) solid++; }
			CHECK(solid > 0, "no-clip: never went through a wall");
			pl.pitch = -600; for (k = 0; k < 60; k++) game_tick(PAD_UP, 0, 0, 0, 0, 0);
			pl.pitch = 0; for (k = 0; k < 60; k++) game_tick(PAD_CROSS, 0, 0, 0, 0, 0);
			for (k = 0; k < 60; k++) game_tick(0, 0, 0, 0, 0, 0);
			z1 = pl.z;
			CHECK(pl_flying && z1 - z0 > 15 * FX, "no-clip: climbed only %d units", (z1 - z0) / FX);
			printf("no-clip: through walls (%d ticks inside them), up %d units, and it stays up\n", solid, (z1 - z0) / FX);
		}
		sx = pl.x; sy = pl.y;
		game_tick(0, PAD_R2, 0, 0, 0, 0);
		alive = 0; for (i = 0; i < nmons; i++) alive += mons[i].alive;
		CHECK(!qp_waiting && alive == 4, "fire did not start the match (%d bots in)", alive);
		CHECK(!pl_flying && world_cell(pl.x >> 8, pl.y >> 8)->type != SOLID_T, "the match did not put you on your feet at a start");
		printf("warm-up: alone until fire (flown to %d,%d), then %d bots in and you at a start\n", sx >> 8, sy >> 8, alive);
	}
	/* CTF on Facing Cubes, 4 bots (you + 2 v 2); you stand at home doing nothing */
	difficulty = 1;
	game_start_quick(fc, GM_CTF, 4); game_quick_begin();
	CHECK(nflags == 2, "Facing Cubes has %d flags", nflags);
	printf("flags: red at (%d,%d), blue at (%d,%d)\n", flags[0].hx >> 8, flags[0].hy >> 8, flags[1].hx >> 8, flags[1].hy >> 8);
	for (t = 0; t < 60 * 60 * 10 && !match_over; t++) {              /* up to 10 minutes */
		int was = flags[0].state | flags[1].state << 4;
		game_tick(0, 0, 0, 0, 0, 0);
		if ((flags[0].state == 1 || flags[1].state == 1) && was != (flags[0].state | flags[1].state << 4)) took++;
		for (i = 0; i < nmons; i++) if (mons[i].alive && mons[i].z - mons[i].eye >= 15 * FX) upper = 1;
		if (pl.dead) pl.health = 100, pl.dead = 0;                      /* keep the human out of it */
	}
	for (i = 0; i < nmons; i++) kills += mons[i].frags;
	printf("CTF after %d s: red %d blue %d, flags taken %d times, bot frags %d, a bot went up the temple: %s\n",
	       t / 60, team_score[0], team_score[1], took, kills, upper ? "yes" : "no");
	CHECK(took > 0, "no flag was ever taken");
	/* (captures are hard 2 v 2: a defender each side and a long run home; checked alone below) */
	game_start_quick(fc, GM_CTF, 1); game_quick_begin();                                   /* one blue attacker, red's base empty */
	for (t = 0; t < 60 * 60 * 4 && !team_score[1]; t++) { game_tick(0, 0, 0, 0, 0, 0); if (pl.dead) pl.health = 100, pl.dead = 0; pl.x = 64 * FX; pl.y = 64 * FX; pl.z = 12 * FX + 819; }
	printf("a lone attacker: %s after %d s\n", team_score[1] ? "scored" : "no capture", t / 60);
	CHECK(team_score[1] > 0, "a bot alone could not capture in 4 minutes");
	CHECK(upper, "no bot ever got onto an upper level (teleporters)");

	/* deathmatch on pig01: bots find each other and fight */
	game_start_quick(map_index("pig01"), GM_DM, 5); game_quick_begin();
	for (t = 0; t < 60 * 60 * 3 && !match_over; t++) { game_tick(0, 0, 0, 0, 0, 0); if (pl.dead) pl.health = 100, pl.dead = 0; }
	for (kills = 0, i = 0; i < nmons; i++) kills += mons[i].frags;
	printf("DM pig01 after %d s: bot frags in all %d, best %d%s\n", t / 60, kills, qp_leader_frags(), match_over ? " (match over)" : "");
	CHECK(kills > 3, "bots hardly fight in deathmatch");

	/* falling off kills you */
	game_start_quick(fc, GM_CTF, 1); game_quick_begin();
	pl.x = 64 * FX; pl.y = 30 * FX;                                       /* over the abyss */
	for (t = 0; t < 600 && !pl.dead; t++) game_tick(0, 0, 0, 0, 0, 0);
	fell = pl.dead;
	printf("stepping into space: %s after %.1f s\n", fell ? "dead" : "still alive", t / 60.0);
	CHECK(fell, "falling into space does not kill");
	{   /* King of the Hill on Facing Cubes: alone on the hill you score a point a second; with bots, they fight over it */
		int hx, hy, hz, ho, k, onhill = 0, held[3] = { 0, 0, 0 };
		game_start_quick(fc, GM_KOTH, 0); game_quick_begin();
		CHECK(koth_hill(&hx, &hy, &hz, &ho), "Facing Cubes has no hill");
		pl.x = hx; pl.y = hy; pl.z = hz + 819;
		for (k = 0; k < 60 * 10 + 5; k++) game_tick(0, 0, 0, 0, 0, 0);
		CHECK(team_score[0] == 10 && team_score[1] == 0, "alone on the hill for 10 s: %d - %d", team_score[0], team_score[1]);
		printf("KOTH: 10 s alone on the hill (%d,%d, floor %d) scores %d - %d\n", hx >> 8, hy >> 8, hz / FX, team_score[0], team_score[1]);
		game_start_quick(fc, GM_KOTH, 5); game_quick_begin();
		pl.x = 20 * FX + 128; pl.y = 64 * FX + 128;                  /* you stay home, out of it */
		for (t = 0; t < 60 * 300 && !match_over; t++) {
			pl.health = 100; game_tick(0, 0, 0, 0, 0, 0);
			koth_hill(&hx, &hy, &hz, &ho); if (ho >= 0) held[ho == 2 ? 2 : ho]++;
			if (!(t % 60)) for (k = 0; k < nmons; k++) if (mons[k].alive && on_hill_test(mons[k].x, mons[k].y)) onhill++;
		}
		printf("KOTH, 5 bots, 5 min: %d - %d; hill red %d s, blue %d s, contested %d s; bot-seconds on the hill %d\n",
		       team_score[0], team_score[1], held[0] / 60, held[1] / 60, held[2] / 60, onhill);
		CHECK(onhill > 60 && held[0] + held[1] > 60 * 30 && held[2] > 60 * 5, "the bots did not fight for the hill");
	}
	printf("RESULT: %s\n", fails ? "FAILED" : "all checks passed");
	return fails != 0;
}
