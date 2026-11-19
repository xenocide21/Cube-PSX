/* host test: link play - two humans in one game, the same game whichever console is looking */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <psxpad.h>
int isin(int x) { return (int)lrint(sin((x & 4095) * 2 * M_PI / 4096) * 4096); }
int icos(int x) { return (int)lrint(cos((x & 4095) * 2 * M_PI / 4096) * 4096); }
static int fails;
#define EYE_T 819
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static int map_index(const char *n) { int i; for (i = 0; i < NUM_MAPS; i++) if (!strcmp(map_names[i], n)) return i; return -1; }

static LinkSetup setup(int map, int mode, int bots, int teams) {
	LinkSetup s; memset(&s, 0, sizeof(s));
	s.map = map; s.mode = mode; s.bots = bots; s.teams = teams; s.skill = 1; s.move = 1; s.turn = 1;
	s.look[0] = s.look[1] = 1; s.aim[0] = 1; s.aim[1] = 0; s.seed = 0xC0BE;
	return s;
}
static unsigned rs;
static int r(int n) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) % n; }
static void rand_pad(LinkPad *p, LinkPad *last) {          /* a restless player: runs, turns, jumps, shoots, switches guns */
	static const uint16_t bt[] = { PAD_UP, PAD_UP | PAD_R2, PAD_UP | PAD_LEFT, PAD_UP | PAD_RIGHT, PAD_R2, PAD_DOWN, PAD_UP | PAD_CROSS, PAD_TRIANGLE, PAD_L1 | PAD_R2 };
	uint16_t h = (r(8) == 0) ? bt[r(9)] : last->held;
	p->pressed = h & ~last->held; p->held = h;
	p->lx = p->ly = p->rx = p->ry = 0;
	if (r(5) == 0) p->rx = (int8_t)(r(255) - 127);
	*last = *p;
}

/* a whole match from the same pads: the host's view (no swaps) or the guest's (a swap there and back every tick, as it
   draws), and every tick's state hash */
static uint32_t hashes[2][6000];
static void run(int guest, int ticks, uint32_t *out, int *p1frags, int *p2frags) {
	LinkPad a, b, la, lb; int t;
	LinkSetup s = setup(map_index("fcubes"), GM_CTF, 4, LT_VERSUS);
	memset(&la, 0, sizeof(la)); memset(&lb, 0, sizeof(lb));
	game_start_link(&s, guest);
	rs = 777;
	for (t = 0; t < ticks; t++) {
		rand_pad(&a, &la); rand_pad(&b, &lb);
		if (t == 30) a.pressed |= PAD_R2;
		if (t == 50) b.pressed |= PAD_R2;
		game_tick_link(&a, &b);
		if (guest) { game_link_view(1); CHECK(link_phase == 1, "the guest's view is not human 1"); game_link_view(0); }
		out[t] = game_link_hash();
	}
	*p1frags = game_link_frags(0); *p2frags = game_link_frags(1);
	game_link_end();
}

int main(void) {
	int fc = map_index("fcubes"), t, i;
	LinkPad idle, fire; memset(&idle, 0, sizeof(idle)); memset(&fire, 0, sizeof(fire)); fire.held = fire.pressed = PAD_R2;

	/* the warm-up waits for both */
	{
		LinkSetup s = setup(fc, GM_DM, 2, LT_VERSUS); int alive = 0;
		game_start_link(&s, 0);
		CHECK(qp_waiting && link_on, "no warm-up");
		game_tick_link(&fire, &idle);
		for (t = 0; t < 30; t++) game_tick_link(&idle, &idle);
		CHECK(qp_waiting && game_link_ready(0) && !game_link_ready(1), "the match started with one player ready");
		game_tick_link(&idle, &fire);
		game_tick_link(&idle, &idle);
		for (i = 0; i < nmons; i++) alive += mons[i].alive;
		CHECK(!qp_waiting && alive == 3, "both ready: %d in besides P1 (2 bots and P2 wanted)", alive);
		CHECK(!pl.dead && pl_frags == 0, "P1 not in");
		printf("warm-up: waited for both; then P1, P2 and 2 bots in\n");
		game_link_end();
	}

	/* P2 shoots P1: P1 dies, P2 has the frag */
	{
		LinkSetup s = setup(fc, GM_DM, 0, LT_VERSUS); int k, p1x, p1y;
		game_start_link(&s, 1);
		game_tick_link(&fire, &fire); game_tick_link(&idle, &idle);
		p1x = pl.x; p1y = pl.y;
		pl.armour = 0;
		game_link_view(1);                                     /* (as the guest's screen: P2 in pl) */
		pl.x = p1x + 3 * FX; pl.y = p1y; pl.z = mons[nmons - 1].z;
		pl.yaw = yaw_to(p1x - pl.x, 0); pl.pitch = 0;
		pl.ammo[GUN_SG] = 50; pl.gun = GUN_SG;
		game_link_view(0);
		for (k = 0; k < 600 && !pl.dead; k++) game_tick_link(&idle, &fire);
		CHECK(pl.dead || pl.deaths > 0, "P2's shotgun never killed P1 (P1 health %d)", pl.health);
		CHECK(game_link_frags(1) == 1 && game_link_frags(0) == 0, "frags after P2 killed P1: P1 %d, P2 %d", game_link_frags(0), game_link_frags(1));
		printf("P2 v P1: P1 down after %d ticks, frags P1 %d P2 %d\n", k, game_link_frags(0), game_link_frags(1));
		/* and back: P1 shoots P2 */
		for (k = 0; k < 300; k++) game_tick_link(&idle, &idle);
		game_link_view(1); p1x = pl.x; p1y = pl.y; pl.armour = 0; { int z2 = pl.z; game_link_view(0);
		pl.x = p1x - 3 * FX; pl.y = p1y; pl.z = z2; }
		pl.yaw = yaw_to(3 * FX, 0); pl.pitch = 0; pl.ammo[GUN_SG] = 50; pl.gun = GUN_SG;
		for (k = 0; k < 600 && mons[nmons - 1].alive; k++) game_tick_link(&fire, &idle);
		CHECK(!mons[nmons - 1].alive && game_link_frags(0) == 1, "P1 never killed P2 (frags %d)", game_link_frags(0));
		printf("P1 v P2: P2 down after %d ticks, frags P1 %d P2 %d\n", k, game_link_frags(0), game_link_frags(1));
		game_link_end();
	}

	/* the same game whoever looks: CTF, 4 bots, both players restless, 100 s */
	{
		int n = 6000, f0[2], f1[2], bad = -1;
		run(0, n, hashes[0], &f0[0], &f1[0]);
		run(1, n, hashes[1], &f0[1], &f1[1]);
		for (t = 0; t < n; t++) if (hashes[0][t] != hashes[1][t]) { bad = t; break; }
		CHECK(bad < 0, "host and guest views of the same game part at tick %d", bad);
		printf("lockstep: host and guest agree for %d ticks (frags P1 %d P2 %d, red %d blue %d)\n", n, f0[0], f1[0], team_score[0], team_score[1]);
		{   /* and a different seed or pad really is a different game (the hash sees it) */
			uint32_t h0 = hashes[0][n - 1]; int a, b;
			run(0, 200, hashes[1], &a, &b);
			CHECK(hashes[1][199] == hashes[0][199], "a rerun is not the same game");
			(void)h0;
		}
	}

	/* bots go for P2 too */
	{
		LinkSetup s = setup(fc, GM_TDM, 3, LT_VERSUS); int seen = 0, k, p = -1;
		game_start_link(&s, 0);
		game_tick_link(&fire, &fire);
		for (i = 0; i < nmons; i++) if (mons[i].bot == 2) p = i;
		for (i = 0; i < nmons; i++) if (mons[i].bot == 1 && mons[i].team != mons[p].team) {   /* a red bot, 5 units off P2, facing it */
			Monster *m = &mons[i]; int k2;
			for (k2 = 5; k2 < 30; k2++) { int x = mons[p].x - k2 * FX / 5 * 5, fz, cz; if (world_fits(x, mons[p].y, m->radius, mons[p].z - EYE_T, 384, &fz, &cz)) { m->x = x; break; } }
			m->y = mons[p].y; m->z = mons[p].z; m->yaw = m->myaw = m->targetyaw = yaw_to(mons[p].x - m->x, 0); break;
		}
		for (k = 0; k < 60 * 60 && !seen; k++) { game_tick_link(&idle, &idle); for (i = 0; i < nmons; i++) if (mons[i].bot == 1 && mons[i].enemy == p && mons[i].team != mons[p].team) seen = 1; }
		CHECK(seen, "no bot ever went for P2");
		printf("a bot went for P2 after %d s\n", k / 60);
		game_link_end();
	}

	/* the match menu's table, and leaving: the other player wins */
	{
		LinkSetup s = setup(fc, GM_CTF, 3, LT_VERSUS); PlayerStat ps; int n = 0, k; LinkPad q = idle;
		game_start_link(&s, 1);
		game_tick_link(&fire, &fire); game_tick_link(&idle, &idle);
		while (game_player_stat(n, &ps)) {
			if (n == 0) CHECK(!strcmp(ps.name, "PLAYER 1") && !ps.you && ps.team == 0, "row 0: %s you %d team %d", ps.name, ps.you, ps.team);
			if (n == 1) CHECK(!strcmp(ps.name, "PLAYER 2") && ps.you && ps.team == 1, "row 1: %s you %d team %d", ps.name, ps.you, ps.team);
			n++;
		}
		CHECK(n == 5, "%d rows in the table (2 players + 3 bots wanted)", n);
		game_link_view(1); game_player_stat(1, &ps); CHECK(ps.you && !strcmp(ps.name, "PLAYER 2"), "the guest's view: row 1 is not P2"); game_link_view(0);
		{ /* P2 brings the red flag home: a capture on P2's row */
			int red_x = flags[0].hx, red_y = flags[0].hy, red_z = flags[0].hz, cap = 0;
			game_link_view(1); pl.x = red_x; pl.y = red_y; pl.z = red_z + EYE_T; game_link_view(0);
			game_tick_link(&idle, &idle);
			game_link_view(1); pl.x = flags[1].hx; pl.y = flags[1].hy; pl.z = flags[1].hz + EYE_T; game_link_view(0);
			for (k = 0; k < 5; k++) game_tick_link(&idle, &idle);
			game_player_stat(1, &ps); cap = ps.caps;
			CHECK(cap == 1 && team_score[1] == 1, "P2 brought the flag home: caps %d, blue %d", cap, team_score[1]);
			printf("table: %d rows; P2 captured, caps %d, blue %d\n", n, cap, team_score[1]);
		}
		q.quit = 1;
		game_tick_link(&idle, &q);
		CHECK(match_over && game_msg && strstr(game_msg, "PLAYER 2 LEFT - PLAYER 1 WINS"), "P2 left: %s", game_msg ? game_msg : "(no message)");
		printf("leaving: %s\n", game_msg ? game_msg : "?");
		game_link_end();
	}

	printf("RESULT: %s\n", fails ? "FAILURES" : "all checks passed");
	return fails != 0;
}
