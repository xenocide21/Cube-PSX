/* host test: Cube's sound events fire where Cube fires them */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
extern int snd_count[64], say_count[128], say_last, say_busy_until;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static void clr(void) { memset(snd_count, 0, sizeof(snd_count)); }
static void tk(int held, int pressed, int n) { while (n--) game_tick(held, pressed, 0, 0, 0, 0), pressed = 0; }

int main(void) {
	int i, k;
	game_start(0);
	for (i = 0; i < nmons; i++) mons[i].alive = 0;
	pl.gun = GUN_SG; pl.ammo[GUN_SG] = 10;
	clr(); tk(PAD_R2, PAD_R2, 1);
	printf("fire shotgun: S_SG x%d\n", snd_count[S_SG]); CHECK(snd_count[S_SG] == 1, "no shotgun sound");
	clr(); pl.ammo[GUN_CG] = 10; tk(0, PAD_TRIANGLE, 1);
	printf("switch weapon: S_WEAPLOAD x%d\n", snd_count[S_WEAPLOAD]); CHECK(snd_count[S_WEAPLOAD] == 1, "no weapon switch sound");
	clr(); tk(0, PAD_CROSS, 1); tk(0, 0, 120);
	printf("jump: S_JUMP x%d\n", snd_count[S_JUMP]); CHECK(snd_count[S_JUMP] == 1, "no jump sound");

	/* a monster: woken by being shot, pain, then death; shotgun pellets count as one hit */
	game_start(0);
	for (i = 1; i < nmons; i++) mons[i].alive = 0;
	{
		Monster *m = &mons[0]; int dx, dy; double a;
		pl.x = m->x - 4 * 256; pl.y = m->y; pl.z = m->z - m->eye + 819;
		dx = m->x - pl.x; dy = m->y - pl.y; a = atan2(-dx, dy); pl.yaw = ((int)lrint(a * 4096 / (2 * M_PI))) & 4095; pl.pitch = 0;
		m->health = 1000; pl.gun = GUN_SG; pl.ammo[GUN_SG] = 10; pl.health = 1000;
		clr(); tk(PAD_R2, PAD_R2, 1);
		printf("shotgun blast into a monster: pain sounds %d\n", snd_count[S_PAINO] + snd_count[S_PAINR] + snd_count[S_PAINE] + snd_count[S_PAINS] + snd_count[S_PAINB] + snd_count[S_PAINP] + snd_count[S_PAINH] + snd_count[S_PAIND]);
		CHECK(snd_count[S_PAINO] + snd_count[S_PAINR] + snd_count[S_PAINE] + snd_count[S_PAINS] + snd_count[S_PAINB] + snd_count[S_PAINP] + snd_count[S_PAINH] + snd_count[S_PAIND] == 1, "one blast should be one pain sound");
		m->health = 1; clr();
		for (k = 0; k < 200 && m->alive; k++) tk(PAD_R2, PAD_R2, 1);
		printf("kill: death sound %s\n", snd_count[S_DIE1] + snd_count[S_DEATHR] + snd_count[S_DEATHE] + snd_count[S_DEATHS] + snd_count[S_DEATHB] + snd_count[S_PIGGR2] + snd_count[S_DEATHH] + snd_count[S_DEATHD] ? "yes" : "no");
		CHECK(!m->alive && snd_count[S_DIE1] + snd_count[S_DEATHR] + snd_count[S_DEATHE] + snd_count[S_DEATHS] + snd_count[S_DEATHB] + snd_count[S_PIGGR2] + snd_count[S_DEATHH] + snd_count[S_DEATHD] == 1, "no death sound");
	}
	/* items, doors, teleports */
	for (k = 0; k < NUM_MAPS; k++) {
		game_start(k);
		for (i = 0; i < nmons; i++) mons[i].alive = 0;
		for (i = 0; i < nitems; i++) {
			Item *it = &items[i];
			if (it->taken) continue;                   /* (pig04: a teleporter beside the rockets lands the player on the boost platform) */
			pl.health = 50; pl.armour = 0; pl.armourtype = 0; memset(pl.ammo, 0, sizeof(pl.ammo));
			pl.x = it->x; pl.y = it->y; pl.z = it->z + 819; clr(); tk(0, 0, 1);
			if (it->taken) CHECK(snd_count[S_ITEMAMMO] + snd_count[S_ITEMHEALTH] + snd_count[S_ITEMARMOUR] >= 1, "%s: item %d picked up silently", map_names[k], i);
		}
		for (i = 0; i < ntrigs; i++) {
			Trig *t = &trigs[i];
			if (t->type != E_TELEPORT && !(t->type == E_CARROT && t->attr1 && t->attr2 != 3)) continue;
			game_start(k); t = &trigs[i]; if (t->type == E_TELEPORT) tk(0, 0, 31);     /* teleports have a 500 ms cooldown (a carrot at the spawn would fire meanwhile) */
			pl.x = t->x + 128; pl.y = t->y + 128; pl.z = t->z + 819; clr(); tk(0, 0, 1);
			if (t->type == E_TELEPORT) CHECK(snd_count[S_TELEPORT] == 1, "%s: silent teleport", map_names[k]);
			else CHECK(snd_count[S_RUMBLE] >= 1, "%s: door opened silently", map_names[k]);     /* (pigfin stacks two carrots on one spot: two rumbles, as in Cube) */
		}
	}
	printf("items, doors and teleports on all maps: checked\n");
	/* water: splash in */
	for (k = 0; k < NUM_MAPS; k++) if (!strcmp(map_names[k], "pig09")) break;
	game_start(k); pl.inwater = 0; clr(); tk(0, 0, 1);
	printf("spawn in pig09's canal: S_SPLASH2 x%d\n", snd_count[S_SPLASH2]); CHECK(snd_count[S_SPLASH2] == 1, "no splash");

	/* the dude's voice */
	{
		int quips = 0, kills = 0, same = 0, prev = -1, grunts;
		game_start(0);
		for (i = 0; i < nmons; i++) mons[i].alive = 0;
		memset(say_count, 0, sizeof(say_count)); say_busy_until = 0;
		Monster m0 = mons[0];
		for (k = 0; k < 300; k++) {                           /* 300 kills, 2 s apart */
			Monster *m = &mons[0]; int n0 = 0, j, dx, dy; double a;
			*m = m0; m->health = 1; m->alive = 1; pl.kills = 0;   /* (or the map counts as cleared after 25) */
			pl.x = m->x - 4 * 256; pl.y = m->y; pl.z = m->z - m->eye + 819;
			dx = m->x - pl.x; dy = m->y - pl.y; a = atan2(-dx, dy); pl.yaw = ((int)lrint(a * 4096 / (2 * M_PI))) & 4095;
			pl.pitch = 0; pl.gun = GUN_SG; pl.ammo[GUN_SG] = 10; pl.health = 100;
			for (j = V_OHYEAH; j <= V_TOOSLOW; j++) n0 += say_count[j];
			for (j = 0; j < 400 && m->alive; j++) { pl.health = 100; pl.dead = 0; tk(PAD_R2, PAD_R2, 1); }
			if (!m->alive) kills++;
			m->alive = 0;
			for (j = 0; j < 119; j++) { pl.health = 100; pl.dead = 0; tk(0, 0, 1); }
			{ int n1 = 0; for (j = V_OHYEAH; j <= V_TOOSLOW; j++) n1 += say_count[j];
			  if (n1 > n0) { quips++; if (say_last == prev) same++; prev = say_last; } }
		}
		printf("kill quips: %d of %d kills (about a third expected), same line twice running: %d\n", quips, kills, same);
		CHECK(kills > 250 && quips > kills / 5 && quips < kills / 2 && same == 0, "kill quip rule");
		memset(say_count, 0, sizeof(say_count)); say_busy_until = 0;
		for (k = 0; k < 30; k++) { pl.health = 100; extern void hurt_player_test(int); hurt_player_test(5); tk(0, 0, 6); }
		grunts = say_count[V_GRUNT1] + say_count[V_GRUNT2] + say_count[V_GRUNT3];
		printf("hit 30 times, 0.1 s apart: %d grunts (0.4 s apart at most)\n", grunts);
		CHECK(grunts >= 5 && grunts <= 9, "grunt rate");
	}
	/* locked door: walk into a closed door and complain once */
	{
		int found = 0, x, y;
		game_start(1);
		for (i = 0; i < nmons; i++) mons[i].alive = 0;
		memset(say_count, 0, sizeof(say_count)); say_busy_until = 0;
		for (y = 2; y < world_ssize() - 2 && !found; y++) for (x = 2; x < world_ssize() - 2 && !found; x++) {
			const Cell *c = world_cell(x, y), *w = world_cell(x - 2, y);
			int fz, cz;
			if (!c->tag || c->type != SOLID_T || w->type != SPACE_T || w->tag) continue;
			if (!world_fits((x - 2) * 256 + 128, y * 256 + 128, 281, w->floor * 256, 384, &fz, &cz)) continue;
			pl.x = (x - 2) * 256 + 128; pl.y = y * 256 + 128; pl.z = fz + 819; pl.yaw = 3072; found = 1;
		}
		CHECK(found, "no closed door to walk into");
		tk(PAD_UP, 0, 120);
		k = say_count[V_LOCKED] + say_count[V_SWITCH] + say_count[V_COMEON];
		printf("walked into a closed door for 2 s: %d complaint(s)\n", k);
		CHECK(k == 1, "should complain exactly once");
	}
	printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: all checks passed\n", fails);
	return fails != 0;
}
