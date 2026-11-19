/* host test: Cube's monster AI (monster.cpp) - waking by sight, aiming at where you were, pain, infighting, random-walk
 * round walls, bodies blocking, and a damage-per-minute table per monster type and difficulty. */
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
#define YAW_PX 3072                      /* facing +x (forward = (-sin yaw, cos yaw)) */
#define YAW_MX 1024

static int ox, oy, oz;                   /* start of a straight, flat, open run along +x */
static int run_len;

static int flat_open(int x, int y, int len) {
	int i, fz, cz, f0 = 0;
	for (i = 0; i <= len; i++) {
		if (!world_fits(x + i * U, y, 4 * 281, 0, 384, &fz, &cz)) return 0;
		if (i == 0) f0 = fz; else if (fz != f0) return 0;
		if (cz - fz < 6 * U) return 0;
	}
	return 1;
}
static void find_run(int len) {
	int x, y;
	for (y = 4; y < world_ssize() - 4; y++)
		for (x = 4; x < world_ssize() - len - 4; x++) {
			int cx = x * U + 128, cy = y * U + 128, fz, cz;
			const Cell *c = world_cell(x, y);
			if (c->tag) continue;
			if (flat_open(cx, cy, len)) { world_fits(cx, cy, 281, 0, 384, &fz, &cz); ox = cx; oy = cy; oz = fz; run_len = len; return; }
		}
	ox = -1;
}

/* clear the level down to the monsters a test places itself */
static void reset(int ntest) {
	int i;
	game_start(0);
	for (i = 0; i < nmons; i++) mons[i].alive = 0;
	for (i = 0; i < MAXPROJ; i++) projs[i].life = 0;
	(void)ntest;
	pl.health = 100; pl.armour = 0; pl.dead = 0; pl.vx = pl.vy = pl.vz = 0;
}
static Monster *put(int i, int type, int x, int yaw, int state) {
	Monster *m = &mons[i]; int fz, cz;
	m->alive = 1; m->type = type; m->mstate = state; m->enemy = -1; m->anger = 0; m->trigger = 0; m->move = state != M_SLEEP;
	m->scale = (int[]){10, 9, 14, 18, 24, 15, 18, 10}[type]; m->radius = 281 * m->scale / 10; m->eye = 512 * m->scale / 10;
	m->health = (int[]){100, 70, 120, 200, 500, 50, 250, 100}[type];
	m->x = x; m->y = oy; world_fits(x, oy, m->radius, oz, 384, &fz, &cz); m->z = fz + m->eye;
	m->yaw = m->targetyaw = yaw; m->onfloor = 1; m->vz = 0; m->blocked = 0; m->painflash = 0; m->anim = ANIM_IDLE;
	return m;
}
static void player_at(int x, int yaw) { pl.x = x; pl.y = oy; pl.z = oz + 819; pl.yaw = yaw; pl.pitch = 0; }
static void ticks(int n) { while (n--) { int hp = pl.health; game_tick(0, 0, 0, 0, 0, 0); (void)hp; } }

int main(void) {
	int i, t;
	game_start(0);
	find_run(26);
	printf("open run on %s at (%d,%d), %d units\n", map_names[0], ox >> 8, oy >> 8, run_len);
	CHECK(ox > 0, "no open run found");
	if (ox < 0) return 1;

	/* ---- waking: the better the angle, the further it sees ---- */
	{
		/* ours: Cube's ranges doubled, and anything in plain sight notices you after a moment (sooner if facing you) */
		struct { int dist, face_player, wake, after; const char *what; } cases[] = {
			{ 24, 1, 1, 60, "24 units, facing you" }, { 12, 0, 1, 60, "12 units, back turned" }, { 6, 0, 1, 60, "6 units, back turned" },
			{ 40, 0, 0, 60, "40 back turned, 1 s" }, { 40, 0, 1, 240, "40 back turned, 4 s" },
		};
		for (i = 0; i < 5; i++) {
			Monster *m; reset(1);
			m = put(0, 0, ox, cases[i].face_player ? YAW_PX : YAW_MX, M_SLEEP);
			player_at(ox + cases[i].dist * U, YAW_MX);
			ticks(cases[i].after);
			printf("wake: %-22s -> %s\n", cases[i].what, m->mstate == M_SLEEP ? "asleep" : "awake");
			CHECK((m->mstate != M_SLEEP) == cases[i].wake, "wake rule wrong: %s", cases[i].what);
		}
	}

	/* ---- aiming: a monster fires at where you were when it started aiming, so stepping aside dodges ---- */
	{
		int dodge, hits[2] = { 0, 0 }, tries[2] = { 0, 0 };
		for (dodge = 0; dodge < 2; dodge++)
			for (t = 0; t < 12; t++) {
				Monster *m; int hp, k;
				reset(2);
				m = put(0, 3, ox, YAW_PX, M_HOME);              /* slith: rifle, hitscan, 80 ms lag */
				player_at(ox + 10 * U, YAW_MX);
				for (k = 0; k < 600 && m->mstate != M_AIMING; k++) ticks(1);
				if (m->mstate != M_AIMING) continue;
				tries[dodge]++;
				if (dodge) pl.y = oy + 2 * U;                  /* side-step 2 units */
				hp = pl.health;
				for (k = 0; k < 60 && m->mstate == M_AIMING; k++) ticks(1);
				if (pl.health < hp) hits[dodge]++;
			}
		printf("aim: standing still hit %d/%d, side-stepped hit %d/%d\n", hits[0], tries[0], hits[1], tries[1]);
		CHECK(tries[0] > 8 && hits[0] == tries[0], "a standing target should always be hit");
		CHECK(tries[1] > 8 && hits[1] == 0, "a side-stepping target should never be hit");
	}

	/* ---- pain: shooting it stuns it, and a stunned monster does not fire ---- */
	{
		Monster *m; int k, fired = 0, hp;
		reset(3);
		m = put(0, 2, ox + 8 * U, YAW_MX, M_HOME);             /* ratamahatta, 400 ms pain */
		player_at(ox, YAW_PX); pl.gun = GUN_CG; pl.ammo[GUN_CG] = 10;
		game_tick(PAD_R2, PAD_R2, 0, 0, 0, 0);
		CHECK(m->mstate == M_PAIN, "hit monster not in pain (state %d)", m->mstate);
		hp = pl.health;
		for (k = 0; k < 20; k++) { ticks(1); if (m->mstate == M_AIMING || m->mstate == M_ATTACKING) fired = 1; }
		printf("pain: state after hit = %s, fired while stunned: %s\n", m->mstate == M_PAIN ? "pain" : "other", fired ? "yes" : "no");
		CHECK(!fired && pl.health == hp, "monster fired while in pain");
	}

	/* ---- infighting: a monster hit by another turns on it ---- */
	{
		Monster *a, *b; int k, turned = -1;
		reset(4);
		a = put(0, 1, ox, YAW_PX, M_HOME);                     /* rhino with a chaingun ... */
		b = put(1, 0, ox + 5 * U, YAW_PX, M_SLEEP);            /* ... and an ogre in its line of fire (loyalty 1) */
		player_at(ox + 12 * U, YAW_MX);
		pl.health = 100000;
		for (k = 0; k < 1200 && turned < 0; k++) { ticks(1); if (b->enemy == 0) turned = k; }
		printf("infight: ogre turned on the rhino after %d ticks; ", turned);
		for (k = 0; k < 1800 && a->alive && b->alive; k++) ticks(1);
		printf("after the fight: rhino %s (hp %d), ogre %s (hp %d)\n", a->alive ? "alive" : "dead", a->health, b->alive ? "alive" : "dead", b->health);
		CHECK(turned >= 0, "ogre never turned on the rhino that shot it");
		CHECK(a->health < 70 || !a->alive, "ogre never hurt the rhino");
	}

	/* ---- blocked by a wall: Cube's "random walk" turns it away to search, then it heads back for you (and may bump
	   the wall again: Cube's monsters are not smart).  Random, so five tries: every one must have searched ---- */
	{
		int trial, freed = 0, tried = 0, wx, fz, cz;
		for (trial = 0; trial < 5; trial++) {
			Monster *m; int k, searched = 0, at_wall = 0;
			reset(5);
			ticks(37 * trial);                                 /* a different stretch of the random numbers each time */
			for (wx = ox; wx > 2 * U && world_fits(wx - U, oy, 281, oz, 384, &fz, &cz); wx -= U) ;
			m = put(0, 0, wx, YAW_MX, M_HOME);
			player_at(wx - 12 * U, YAW_MX);
			pl.dead = 1;
			for (k = 0; k < 900; k++) {
				ticks(1);
				if (m->mstate == M_SEARCH) searched++;
				if (iabs(m->x - wx) < U / 2 && iabs(m->y - oy) < U / 2) at_wall++;
			}
			printf("walk %d: %d ticks searching, %d of 900 ticks at the wall\n", trial, searched, at_wall);
			if (searched > 0) tried++;
			if (at_wall < 450) freed++;
		}
		printf("walk: searched in %d of 5 tries, spent most of the time away from the wall in %d\n", tried, freed);
		CHECK(tried == 5 && freed >= 1, "monster never tried a way round the wall");
	}

	/* ---- bodies: you cannot walk through a monster ---- */
	{
		Monster *m; int k;
		reset(6);
		m = put(0, 0, ox + 6 * U, YAW_MX, M_SLEEP);
		player_at(ox, YAW_PX);
		for (k = 0; k < 300; k++) { pl.health = 100; m->mstate = M_SLEEP; m->trigger = 1 << 30; game_tick(PAD_UP, 0, 0, 0, 0, 0); }
		printf("bodies: player stopped at %d.%d units from the ogre\n", (m->x - pl.x) / U, (m->x - pl.x) % U * 10 / U);
		CHECK(pl.x < m->x - 281, "player walked through a monster");
	}

	/* ---- damage per minute, standing 12 units in front of an awake monster ---- */
	{
		static const char *names[8] = { "ogre", "rhino", "ratamahatta", "slith", "bauul", "hellpig", "knight", "goblin" };
		int ty, df;
		printf("\ndamage taken per minute, standing still 12 units away (hellpig: 2 units)\n%-12s %8s %8s %8s\n", "", "easy", "normal", "hard");
		for (ty = 0; ty < 8; ty++) {
			int dpm[3];
			for (df = 0; df < 3; df++) {
				Monster *m; int k, taken = 0;
				difficulty = df;
				reset(7);
				m = put(0, ty, ox, YAW_PX, M_HOME);
				player_at(ox + (ty == 5 ? 2 : 12) * U, YAW_MX);
				for (k = 0; k < 3600; k++) {
					int hp = 100000; pl.health = hp; pl.armour = 0;
					m->x = ox; m->y = oy;                          /* hold it in place */
					ticks(1);
					taken += hp - pl.health;
				}
				dpm[df] = taken;
			}
			printf("%-12s %8d %8d %8d\n", names[ty], dpm[0], dpm[1], dpm[2]);
			CHECK(dpm[0] < dpm[1] && dpm[1] < dpm[2], "%s: difficulty does not order the damage", names[ty]);
		}
		difficulty = 1;
	}

	printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: all checks passed\n", fails);
	return fails != 0;
}
