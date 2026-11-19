/* host test: profiles on the memory card - create, progress and unlocking (stepping over maps not ported yet), options
 * kept, a game saved from the pause menu restores the map, player, monsters, items, carrots and opened doors */
#include "cube.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
int main(void) {
	int i, door, nx; char line[64];
	remove("cubepsx_save1.mcd"); remove("cubepsx_save2.mcd");
	opt_move = 4; opt_sfx = 6; opt_look = 7; opt_invert = 1; opt_aim = 0; opt_speech = 9;
	CHECK(prof_new(1, "TESTER", 2), "profile not written");
	prof_describe(1, line); printf("new profile: %s\n", line);
	/* first map of each episode open, nothing else */
	for (i = 0; i < CAT_N; i++) {
		int first = i == episodes[catalog[i].episode].first;
		CHECK(!!(prof.maps[i].flags & MR_OPEN) == first, "%s open %d", catalog[i].name, prof.maps[i].flags);
	}
	/* the whole Pigcam chain in Cube's order */
	{
		static const char *chain[] = { "pig02", "pig03", "pig04", "pig05", "pig06", "pig07", "pig08", "pig09", "pigfin" };
		nx = cat_of_world(0);
		for (i = 0; i < 9; i++) {
			nx = prof_complete(nx, i == 0, 300, 10);
			CHECK(nx >= 0 && !strcmp(catalog[nx].name, chain[i]), "step %d should open %s", i, chain[i]);
			if (nx < 0) break;
		}
		if (nx >= 0) { nx = prof_complete(nx, 0, 300, 10); CHECK(nx < 0, "pigfin is the end"); }
	}
	/* best time kept */
	prof_complete(cat_of_world(0), 0, 150, 3); CHECK(prof.maps[cat_of_world(0)].best_secs == 150, "best time not kept");
	prof_complete(cat_of_world(0), 0, 400, 3); CHECK(prof.maps[cat_of_world(0)].best_secs == 150, "worse time replaced the best");
	CHECK(prof.maps[cat_of_world(0)].flags & MR_ALLKILLS, "all-kills mark lost");

	/* a saved game in the profile */
	game_start(1); difficulty = 2;
	for (i = 0; i < ntrigs; i++) if (trigs[i].type == E_CARROT && trigs[i].attr1 == 2) { pl.x = trigs[i].x; pl.y = trigs[i].y; pl.z = trigs[i].z + 819; }
	game_tick(0, 0, 0, 0, 0, 0);                         /* opens door tag 2 */
	door = world_cell(104, 30)->type;
	pl.health = 37; pl.ammo[GUN_RL] = 7; mons[3].alive = 0; mons[3].health = -5; pl.kills = 1; items[2].taken = 1;
	SEEN_MARK(40, 50); SEEN_MARK(41, 51);                     /* the automap has seen a spot */
	CHECK(prof_save_game(cat_of_world(1)), "save game failed");

	/* everything forgotten, then loaded from the card */
	memset(&prof, 0, sizeof(prof)); prof_slot = -1; opt_move = 1; opt_sfx = 8; opt_look = 1; opt_invert = 0; opt_aim = 1; opt_speech = 8;
	game_start(0); difficulty = 0;
	CHECK(prof_load(1), "profile load failed");
	prof_describe(1, line); printf("loaded: %s, saved game on %s\n", line, catalog[prof.state_cat].name);
	CHECK(!strcmp(prof.name, "TESTER") && prof.diff == 2 && opt_move == 4 && opt_sfx == 6, "profile fields / options not restored");
	CHECK(opt_look == 7 && opt_invert == 1 && opt_aim == 0 && opt_speech == 9, "look / invert / aim assist / speech options not restored");
	CHECK(prof.maps[cat_of_world(6)].flags & MR_BEATEN, "progress not restored");
	CHECK(prof.has_state && prof.state_cat == cat_of_world(1), "saved game not recorded");
	CHECK(prof_resume(), "resume failed");
	CHECK(SEEN(40, 50) && !SEEN(60, 50), "automap memory not restored with the saved game");
	CHECK(game_current_map() == 1 && pl.health == 37 && pl.ammo[GUN_RL] == 7 && mons[3].alive == 0 && items[2].taken == 1 && difficulty == 2, "game state not restored");
	CHECK(world_cell(104, 30)->type == door && door == SPACE_T, "opened door not restored");

	{	/* a save made before the pistol (version 4: the player's ammo one slot shorter) still loads */
		static uint8_t old[16384]; int cut = 24 /* magic..total */ + offsetof(Player, ammo) + (NUMGUNS - 1) * sizeof(int), sz = game_save_size();
		game_start(1); pl.health = 55; pl.ammo[GUN_RIFLE] = 9; pl.kills = 3; pl.inwater = 0; items[1].taken = 1;
		game_save(old);
		memmove(old + cut, old + cut + sizeof(int), sz - cut - sizeof(int));   /* drop the pistol slot */
		old[4] = 4; old[5] = 0;
		game_start(0);
		CHECK(game_load(old), "old (v4) save refused");
		CHECK(game_current_map() == 1 && pl.health == 55 && pl.ammo[GUN_RIFLE] == 9 && pl.ammo[GUN_PISTOL] == 0 && pl.kills == 3 && items[1].taken == 1,
		      "old (v4) save not converted");
		printf("old v4 save: converted and loaded (rifle %d, pistol %d, kills %d)\n", pl.ammo[GUN_RIFLE], pl.ammo[GUN_PISTOL], pl.kills);
	}
	CHECK(world_cell(40, 18)->type == SOLID_T, "a door not opened should stay shut");
	/* finishing the saved map clears the saved game */
	prof_complete(cat_of_world(1), 0, 500, 20);
	CHECK(!prof.has_state, "finished map's saved game should be dropped");
	CHECK(prof_describe(2, line) == 0, "empty slot described as used");
	printf("save state %d bytes (room: %d)\n", game_save_size(), 16384 - 0x300);
	CHECK(game_save_size() <= 16384 - 0x300, "save state too big");
	printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: all checks passed\n", fails);
	return fails != 0;
}
