/* bots.inc - quickplay: deathmatch, team deathmatch and capture the flag against bots.  Included by game.c, after the
 * monster code: a bot is a monster (drawn as Cube's ogro, Cube's player model) that plays like a player - player guns at
 * full damage, ammo and items, respawning - with its own thinking instead of Cube's monster AI.
 *
 * Getting about: distance maps.  For a goal, every walkable cell holds how many steps it is from the goal (255 = no way),
 * worked out backwards from the goal with teleporters as one-step shortcuts; a bot walks downhill.  Four maps of
 * 128 x 128 bytes: the two flag bases (or two roaming spots in deathmatch) and one that follows a moving goal per team. */

int game_mode, team_score[2], pl_team, pl_frags, pl_caps, match_over, match_limit;
Flag flags[2]; int nflags;

#define NAVN 128
#define NAVF 4
static uint8_t *navf[NAVF];                     /* distance maps } in the map area the map leaves free (world_spare): */
static uint8_t *walk;                           /* bit 0: a body can stand here; bit 1: beside a drop into the abyss */
static int nav_on;
static uint16_t navq[4096];                     /* breadth-first queue (a ring: the front is never this wide) */
static int nav_goal[NAVF], nav_time[NAVF];
static int nbots, spawn_t0;
static struct { int on, x0, y0, x1, y1, z, owner, acc[2]; } hill;       /* King of the Hill (cells; z in units) */
int koth_hill(int *x, int *y, int *z, int *owner) {
	if (!hill.on || game_mode != GM_KOTH) return 0;
	*x = (hill.x0 + hill.x1) * FX / 2; *y = (hill.y0 + hill.y1) * FX / 2; *owner = hill.owner;
	*z = world_cell(*x >> 8, *y >> 8)->floor * FX;           /* (on top of whatever stands in the middle) */
	return 1;
}
int qp_waiting;
static int ntp; static uint16_t tp_src[16], tp_dst[16];      /* teleporters: the cell it stands in, the cell it lands in */
static int pl_respawn_at;

#define ABYSS_Z (-60 * FX)                      /* fall below this and space has you */
#define BOT_RUN 55                              /* a bot's pace, % of the player's run */
#define BOT_WALK 30

static int nav_ok(int x, int y) { return nav_on && (unsigned)x < NAVN && (unsigned)y < NAVN && (walk[y * NAVN + x] & 1); }
static int cfloor(int x, int y) { return world_cell(x, y)->floor; }

static void nav_init(void) {
	int x, y, n = world_ssize(), room;
	uint8_t *mem = world_spare(&room);
	nav_on = 0;
	if (!mem || room < (NAVF + 1) * NAVN * NAVN || n > NAVN) return;      /* (quickplay maps are 128 or smaller) */
	for (x = 0; x < NAVF; x++) navf[x] = mem + x * NAVN * NAVN;
	walk = mem + NAVF * NAVN * NAVN; nav_on = 1;
	memset(walk, 0, NAVN * NAVN);
	for (y = 2; y < n - 2 && y < NAVN; y++)
		for (x = 2; x < n - 2 && x < NAVN; x++) {
			const Cell *c = world_cell(x, y); int fz, cz;
			if (c->type == SOLID_T || c->type == CORNER_T || c->ceil - c->floor < 5 || c->floor * FX < ABYSS_Z + 20 * FX) continue;
			/* a whole body (2.2 cells across) must fit standing in the middle of it - the same test movement uses */
			if (!world_fits(x * FX + 128, y * FX + 128, PRAD, c->floor * FX, STEPMAX, &fz, &cz) || fz > c->floor * FX + STEPMAX) continue;
			walk[y * NAVN + x] = 1;
		}
	for (y = 1; y < NAVN - 1; y++)                     /* cells on the edge of the abyss */
		for (x = 1; x < NAVN - 1; x++) {
			int dx, dy;
			if (!(walk[y * NAVN + x] & 1)) continue;
			for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++)
				if (world_cell(x + dx, y + dy)->floor * FX < ABYSS_Z + 20 * FX && world_cell(x + dx, y + dy)->type != SOLID_T) walk[y * NAVN + x] |= 2;
		}
	memset(nav_goal, 0xff, sizeof(nav_goal));
	ntp = 0;
	for (y = 0; y < ntrigs && ntp < 16; y++) {
		const Trig *t = &trigs[y];
		if (t->type != E_TELEPORT) continue;
		for (x = 0; x < world_nents(); x++) {
			const MapEnt *e = world_ent(x);
			if (e->type == E_TELEDEST && e->attr2 == t->attr1 && e->x < NAVN && e->y < NAVN) {
				tp_src[ntp] = (t->y >> 8) * NAVN + (t->x >> 8); tp_dst[ntp++] = e->y * NAVN + e->x; break;
			}
		}
	}
}

/* can you get from cell (ax,ay) to its neighbour (bx,by)?  a step up of a unit at most, any drop */
static int nav_step(int ax, int ay, int bx, int by) {
	return nav_ok(bx, by) && cfloor(bx, by) - cfloor(ax, ay) <= 1 && world_cell(bx, by)->ceil - cfloor(ax, ay) >= 5;
}

static void nav_build(int f, int gx, int gy) {
	uint8_t *d = navf[f]; int head = 0, tail = 0, i;
	if (!nav_on) return;
	memset(d, 255, NAVN * NAVN);
	nav_goal[f] = gy * NAVN + gx; nav_time[f] = game_ticks;
	if (!nav_ok(gx, gy)) return;
	d[gy * NAVN + gx] = 0; navq[tail++ & 4095] = gy * NAVN + gx;
	while (head != tail) {
		int c = navq[head++ & 4095], cx = c & (NAVN - 1), cy = c / NAVN, v = d[c], k;
		static const int8_t dir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		if (v >= 254) continue;
		for (k = 0; k < 4; k++) {                   /* who can step onto c? */
			int nx = cx + dir[k][0], ny = cy + dir[k][1], n = ny * NAVN + nx, j;
			if (nav_step(nx, ny, cx, cy)) { if (d[n] == 255) { d[n] = v + 1; navq[tail++ & 4095] = n; } continue; }
			/* a drop: a body standing on a ledge overhangs the cells just below its edge, so those are not "walkable" -
			   from 2 or 3 cells away, over open space no higher than the ledge, you step off and land here */
			for (j = 2; j <= 3; j++) {
				int fx2 = cx + dir[k][0] * j, fy2 = cy + dir[k][1] * j, n2 = fy2 * NAVN + fx2, mid = cfloor(nx, ny);
				if (!nav_ok(fx2, fy2) || d[n2] != 255 || cfloor(fx2, fy2) <= cfloor(cx, cy) + 1) continue;
				if (world_cell(nx, ny)->type == SOLID_T || mid > cfloor(fx2, fy2)) continue;
				if (j == 3) { int mx = cx + dir[k][0] * 2, my = cy + dir[k][1] * 2; if (world_cell(mx, my)->type == SOLID_T || cfloor(mx, my) > cfloor(fx2, fy2)) continue; }
				d[n2] = v + j; navq[tail++ & 4095] = n2; break;
			}
		}
		/* teleporters: if c is where one lands, standing at the teleporter is a step away */
		for (i = 0; i < ntp; i++)
			if (tp_dst[i] == c) { int n = tp_src[i]; if ((walk[n] & 1) && d[n] == 255) { d[n] = v + 1; navq[tail++ & 4095] = n; } }
	}
}

/* rebuild a moving goal's map when its goal has changed cell, at most twice a second */
static void nav_goal_at(int f, int x, int y) {
	int gx = x >> 8, gy = y >> 8, k;
	if (!nav_ok(gx, gy)) {                           /* the nearest walkable cell */
		for (k = 1; k < 4; k++) {
			if (nav_ok(gx + k, gy)) { gx += k; break; } if (nav_ok(gx - k, gy)) { gx -= k; break; }
			if (nav_ok(gx, gy + k)) { gy += k; break; } if (nav_ok(gx, gy - k)) { gy -= k; break; }
		}
	}
	if (gy * NAVN + gx == nav_goal[f] || game_ticks - nav_time[f] < 30) return;
	nav_build(f, gx, gy);
}

static int nav_was_drop;                         /* nav_dir's last answer was a step off a ledge */
/* the way downhill from (x,y) on map f: the yaw to the next cell's middle, or -1 (there / no way) */
static int nav_dir(int f, int x, int y) {
	const uint8_t *d = navf[f]; int cx = x >> 8, cy = y >> 8, v, best, bx = -1, by = -1, k, bedge = 9;
	if (!nav_on) return -1;
	static const int8_t dir[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	nav_was_drop = 0;
	if ((unsigned)cx >= NAVN || (unsigned)cy >= NAVN) return -1;
	v = best = d[cy * NAVN + cx];
	if (v == 0 || v == 255) return -1;
	for (k = 0; k < 8; k++) {
		int nx = cx + dir[k][0], ny = cy + dir[k][1], w, e;
		if (!nav_ok(nx, ny)) continue;
		if (k >= 4 && (!nav_ok(cx + dir[k][0], cy) || !nav_ok(cx, cy + dir[k][1]))) continue;      /* no corner cutting */
		w = d[ny * NAVN + nx]; e = walk[ny * NAVN + nx] >> 1;
		if (w < best || (w == best && w < v && e < bedge)) { best = w; bx = nx; by = ny; bedge = e; }
	}
	if (bx < 0) {                                   /* maybe a drop off a ledge: a cell 2 or 3 away, lower, over open space */
		for (k = 0; k < 4; k++) {
			int j;
			for (j = 2; j <= 3; j++) {
				int nx = cx + dir[k][0] * j, ny = cy + dir[k][1] * j, w;
				if (!nav_ok(nx, ny) || cfloor(nx, ny) >= cfloor(cx, cy) - 1 || world_cell(cx + dir[k][0], cy + dir[k][1])->type == SOLID_T) continue;
				w = d[ny * NAVN + nx];
				if (w < best) { best = w; bx = nx; by = ny; }
			}
		}
		if (bx < 0) return -1;
		nav_was_drop = 1;
	}
	return yaw_of((bx * FX + 128) - x, (by * FX + 128) - y);
}

/* ---- teams ---- */
static int team_of(int who) { return who < 0 ? pl_team : mons[who].team; }
static int hum_of(int who);                     /* -1 / the proxy -> which human (0, 1); else -1 (linkplay.inc) */
static int my_team(void);                       /* this console's human's team */
static int foes(int a, int b) {                   /* -1 = the player */
	if (a == b) return 0;
	if (game_mode == GM_DM) return 1;
	return team_of(a) != team_of(b);
}

/* ---- spawning ---- */
static void spawn_point(int team, int *x, int *y, int *yaw) {
	int i, n = 0, pick, cand[64], k;
	/* team starts first (CTF maps); else any start, and in deathmatch the monsters' places too (Cube's SP maps have one start) */
	for (k = 0; k < 2 && !n; k++)
		for (i = 0; i < world_nents() && n < 64; i++) {
			const MapEnt *e = world_ent(i);
			if (k == 0 && e->type == E_PLAYERSTART && ((game_mode != GM_CTF && game_mode != GM_KOTH) || e->attr2 == team)) cand[n++] = i;
			if (k == 1 && (e->type == E_PLAYERSTART || (game_mode != GM_CTF && e->type == E_MONSTER))) cand[n++] = i;
		}
	if (game_mode != GM_CTF && n < 8)
		for (i = 0; i < world_nents() && n < 64; i++) if (world_ent(i)->type == E_MONSTER) cand[n++] = i;
	if (!n) { *x = *y = 64 * FX; *yaw = 0; return; }
	pick = cand[rnd(n)];
	{   /* of a dozen tries, the start furthest from anyone alive (no spawning into a shotgun) */
		int bestd = -1;
		for (k = 0; k < 12; k++) {
			int c = cand[rnd(n)], px = world_ent(c)->x * FX + 128, py = world_ent(c)->y * FX + 128, j, near = 1 << 30;
			if (!pl.dead) near = iabs(pl.x - px) + iabs(pl.y - py);
			for (j = 0; j < nmons; j++) if (mons[j].alive) { int dd = iabs(mons[j].x - px) + iabs(mons[j].y - py); if (dd < near) near = dd; }
			if (near > bestd) { bestd = near; pick = c; }
		}
	}
	*x = world_ent(pick)->x * FX + 128; *y = world_ent(pick)->y * FX + 128; *yaw = (world_ent(pick)->attr1 * 4096 / 360) & 4095;
}

static void bot_spawn(int mi) {
	Monster *m = &mons[mi]; int x, y, yaw, z;
	spawn_point(m->team, &x, &y, &yaw);
	z = world_cell(x >> 8, y >> 8)->floor * FX + m->eye;
	entinmap(&x, &y, &z, m->eye, m->radius);
	m->x = x; m->y = y; m->z = z; m->yaw = m->targetyaw = m->myaw = yaw;
	m->health = 100; m->alive = 1; m->anim = ANIM_IDLE; m->animt0 = game_ticks; m->vz = 0; m->onfloor = 1;
	memset(m->ammo, 0, sizeof(m->ammo));
	m->ammo[GUN_FIST] = 1; m->ammo[GUN_PISTOL] = 20; m->gun = GUN_PISTOL; m->gunwait = 0;    /* fist and pistol: the rest is found */
	m->enemy = -2; m->respawn = 0; m->stuckt = game_ticks; m->sx = x; m->sy = y;
}

static void player_spawn_quick(void) {
	int x, y, yaw;
	player_respawn();
	memset(pl.ammo, 0, sizeof(pl.ammo)); pl.ammo[GUN_FIST] = 1; pl.ammo[GUN_PISTOL] = 20; pl.gun = GUN_PISTOL;   /* quickplay: fist and pistol, find the rest */
	spawn_point(pl_team, &x, &y, &yaw);
	pl.x = x; pl.y = y; pl.yaw = yaw;
	{ int z = world_cell(x >> 8, y >> 8)->floor * FX + EYE; entinmap(&pl.x, &pl.y, &z, EYE, PRAD); pl.z = z; }
	pl_respawn_at = 0;
}

/* ---- flags ---- */
static const char *team_name[2] = { "RED", "BLUE" };
static char qp_msg[64];
static void flag_home(int t) { Flag *f = &flags[t]; f->x = f->hx; f->y = f->hy; f->z = f->hz; f->state = 0; f->carrier = -2; }
static void flag_drop(int who) {
	int t;
	for (t = 0; t < nflags; t++)
		if (flags[t].state == 1 && flags[t].carrier == who) {
			Flag *f = &flags[t]; f->state = 2; f->timer = 30 * 60;
			if (who < 0) { f->x = pl.x; f->y = pl.y; f->z = pl.z - EYE; } else { f->x = mons[who].x; f->y = mons[who].y; f->z = mons[who].z - mons[who].eye; }
			if (f->z < ABYSS_Z) { flag_home(t); sprintf(qp_msg, "THE %s FLAG RETURNS", team_name[t]); say_all(qp_msg); }
			else { sprintf(qp_msg, "THE %s FLAG IS DOWN", team_name[t]); say_all(qp_msg); }
		}
}
static void match_end(const char *why) {
	if (match_over) return;
	match_over = 1; level_done = 3;
	sprintf(level_msg, "\n\n\n\n\n\n   %s\n\n   PRESS START", why);
	game_msg = level_msg; game_msg_timer = 1 << 30; game_msg_for = -1;
	snd_say_any(V_WHOSNEXT, 1);
}

/* someone (-1 = the player) at (x,y,feet) of team `team` touches the flags */
static void flag_touch(int who, int team, int x, int y, int feet) {
	int t;
	for (t = 0; t < nflags; t++) {
		Flag *f = &flags[t];
		if (f->state == 1 || iabs(x - f->x) > 2 * FX || iabs(y - f->y) > 2 * FX || iabs(feet - f->z) > 3 * FX) continue;
		if (t != team) {                            /* take theirs */
			f->state = 1; f->carrier = who;
			if (who < 0 || who == link_P) { sprintf(qp_msg, "YOU HAVE THE %s FLAG!", team_name[t]); say(qp_msg); if (who >= 0) game_msg_for = 1 - link_phase; }
			else { sprintf(qp_msg, "%s FLAG TAKEN", team_name[t]); say_all(qp_msg); }
			snd_play(S_ITEMARMOUR);
		} else if (f->state == 2) {                 /* ours, lying about: back home */
			flag_home(t); sprintf(qp_msg, "THE %s FLAG RETURNS", team_name[t]); say_all(qp_msg); snd_play(S_ITEMHEALTH);
		} else if (flags[1 - t].state == 1 && flags[1 - t].carrier == who) {   /* ours at home, theirs in hand: score */
			flag_home(1 - t); team_score[t]++;
			if (who < 0) pl_caps++; else mons[who].caps++;
			sprintf(qp_msg, "%s SCORES! %d - %d", team_name[t], team_score[0], team_score[1]); say_all(qp_msg);
			snd_say_any(t == my_team() ? V_OHYEAH : V_HURT, 1);
			if (team_score[t] >= match_limit) { sprintf(qp_msg, "%s TEAM WINS %d - %d", team_name[t], team_score[0], team_score[1]); match_end(qp_msg); }
		}
	}
}

static void flags_update(void) {
	int t, i;
	for (t = 0; t < nflags; t++) {
		Flag *f = &flags[t];
		if (f->state == 1) {
			if (f->carrier < 0) { f->x = pl.x; f->y = pl.y; f->z = pl.z - EYE; }
			else { f->x = mons[f->carrier].x; f->y = mons[f->carrier].y; f->z = mons[f->carrier].z - mons[f->carrier].eye; }
		} else if (f->state == 2 && --f->timer <= 0) { flag_home(t); sprintf(qp_msg, "THE %s FLAG RETURNS", team_name[t]); say_all(qp_msg); }
	}
	if (!pl.dead) flag_touch(-1, pl_team, pl.x, pl.y, pl.z - EYE);
	for (i = 0; i < nmons; i++) if (mons[i].bot && mons[i].alive) flag_touch(i, mons[i].team, mons[i].x, mons[i].y, mons[i].z - mons[i].eye);
}

/* ---- kills ---- */
static void frag(int killer, int victim) {          /* -1 = the player; killer == victim or < -1: fell / own rocket */
	if (victim >= 0) mons[victim].deaths++;
	if (killer == victim || killer < -1) { if (victim < 0) pl_frags--; else mons[victim].frags--; }
	else if (!foes(killer, victim)) { if (killer < 0) pl_frags--; else mons[killer].frags--; }
	else {
		if (killer < 0) pl_frags++; else mons[killer].frags++;
		if (game_mode == GM_TDM) team_score[team_of(killer)]++;
	}
	flag_drop(victim);
	if (game_mode == GM_DM) {
		int best = killer < 0 ? pl_frags : killer >= 0 ? mons[killer].frags : 0;
		if (best >= match_limit) {
			int h = hum_of(killer);
			if (link_on && h >= 0) sprintf(qp_msg, "PLAYER %d WINS WITH %d FRAGS", h + 1, best);
			else sprintf(qp_msg, killer < 0 ? "YOU WIN WITH %d FRAGS" : "A BOT WINS WITH %d FRAGS", best);
			match_end(qp_msg);
		}
	} else if (game_mode == GM_TDM && killer != victim && killer >= -1 && team_score[team_of(killer)] >= match_limit) {
		sprintf(qp_msg, "%s TEAM WINS %d - %d", team_name[team_of(killer)], team_score[0], team_score[1]); match_end(qp_msg);
	}
}

static void bot_died(int mi, int attacker) {
	Monster *m = &mons[mi];
	frag(attacker, mi);
	m->respawn = game_ticks + 3 * 60;
}

int qp_leader_frags(void) { int i, b = -999; for (i = 0; i < nmons; i++) if (mons[i].bot == 1 && mons[i].frags > b) b = mons[i].frags; return b; }

/* ---- items ---- */
static int item_respawn(int type) { return (type >= E_GREENARMOUR ? 25 : type >= E_HEALTH ? 20 : 16) * 60; }
static void bot_items(int mi) {
	Monster *m = &mons[mi]; int i;
	for (i = 0; i < nitems; i++) {
		Item *it = &items[i]; int ok = 0;
		if (it->taken || iabs(m->x - it->x) > 2 * FX || iabs(m->y - it->y) > 2 * FX || iabs(m->z - m->eye - it->z) > 3 * FX) continue;
		switch (it->type) {
		case E_SHELLS: if (m->ammo[GUN_SG] < 50) { m->ammo[GUN_SG] += 10; ok = 1; } break;
		case E_BULLETS: if (m->ammo[GUN_CG] < 100) { m->ammo[GUN_CG] += 20; ok = 1; } break;
		case E_ROCKETS: if (m->ammo[GUN_RL] < 25) { m->ammo[GUN_RL] += 5; ok = 1; } break;
		case E_ROUNDS: if (m->ammo[GUN_RIFLE] < 25) { m->ammo[GUN_RIFLE] += 5; ok = 1; } break;
		case E_HEALTH: if (m->health < 100) { m->health += 25; if (m->health > 100) m->health = 100; ok = 1; } break;
		case E_BOOST: if (m->health < 200) { m->health += 50; if (m->health > 200) m->health = 200; ok = 1; } break;
		case E_CLIPS: if (m->ammo[GUN_PISTOL] < 60) { m->ammo[GUN_PISTOL] += 15; ok = 1; } break;
		default: ok = 0;
		}
		if (ok) { it->taken = 1; it->respawn = game_ticks + item_respawn(it->type); snd_play_at(S_ITEMAMMO, it->x, it->y, it->z); }
	}
}
static void items_respawn(void) {
	int i;
	for (i = 0; i < nitems; i++) if (items[i].taken && items[i].respawn && game_ticks >= items[i].respawn) items[i].taken = 0, items[i].respawn = 0;
}

/* ---- fighting ---- */
static void who_pos(int who, int *x, int *y, int *z, int *alive) {
	if (who < 0) { *x = pl.x; *y = pl.y; *z = pl.z; *alive = !pl.dead; }
	else { *x = mons[who].x; *y = mons[who].y; *z = mons[who].z; *alive = mons[who].alive; }
}

static const int8_t bot_hit[3] = { 30, 40, 52 };         /* % of shots on target at best (close, still target, aim settled) */
static const int8_t bot_react[3] = { 42, 26, 15 };       /* ticks an enemy must be in sight before the first shot */
static const uint8_t bot_settle[3] = { 90, 70, 50 };     /* ticks for the aim to settle on a new enemy */

static int link_other_speed(void);
static void bot_pick_enemy(int mi) {
	Monster *m = &mons[mi]; int i, best = 40 * FX, bi = -2, x, y, z, a, was = m->enemy;
	for (i = -1; i < nmons; i++) {
		if (i == mi || (i >= 0 && !mons[i].bot)) continue;
		if (!foes(mi, i)) continue;
		who_pos(i, &x, &y, &z, &a);
		if (!a) continue;
		{ int d = vlen3(x - m->x, y - m->y, z - m->z), off = iabs(((yaw_of(x - m->x, y - m->y) - m->yaw + 2048) & 4095) - 2048);
		  /* sees no further than you do (your view distance right now, fog and all), and not behind itself - unless
		     close, already fighting it, or just hit */
		  if ((i < 0 || i == link_P) && d > sight_cells() * FX) continue;
		  if (off > 800 && d > 6 * FX && i != was && !m->painflash) continue;
		  if (i >= 0 && i != link_P && game_mode != GM_DM) d += 2 * FX;   /* in teams, the humans first */
		  if (d < best && world_los(m->x, m->y, m->z, x, y, z)) { best = d; bi = i; } }
	}
	m->enemy = bi;
	if (bi != was) {                                         /* a new enemy: a moment to react, longer if it is out in the fog */
		int v = sight_cells() * FX, fog = v * 5 / 8, extra = 0;
		if (bi == -1 || bi == link_P) { who_pos(bi, &x, &y, &z, &a); { int d = vlen3(x - m->x, y - m->y, z - m->z); if (d > fog) extra = (d - fog) * 40 / (v - fog + 1); } }
		m->think = game_ticks + bot_react[difficulty] + extra; m->aimt = game_ticks;
	}
}


static void bot_fire(int mi) {
	Monster *m = &mons[mi]; int x, y, z, a, d, g, fx = m->x, fy = m->y, fz = m->z - 51, du, err, tx, ty, tz;
	who_pos(m->enemy, &x, &y, &z, &a);
	if (!a) return;
	z -= 200;                                                /* the chest */
	d = vlen3(x - fx, y - fy, z - fz); du = d / FX;
	if (m->ammo[GUN_RL] > 0 && du > 6 && du < 34) g = GUN_RL;
	else if (m->ammo[GUN_RIFLE] > 0 && du > 16) g = GUN_RIFLE;
	else if (m->ammo[GUN_SG] > 0 && du < 14) g = GUN_SG;
	else if (m->ammo[GUN_CG] > 0) g = GUN_CG;
	else if (m->ammo[GUN_SG] > 0) g = GUN_SG;
	else if (m->ammo[GUN_PISTOL] > 0) g = GUN_PISTOL;
	else if (du < 3) g = GUN_FIST;
	else return;
	m->gun = g;
	if (g != GUN_FIST) m->ammo[g]--;
	m->gunwait = guns[g].delay;
	if (g == GUN_CG && ++m->aware >= 5 + rnd(4)) { m->aware = 0; m->gunwait += 25 + rnd(25); }   /* bursts, as a person fires: 5-8 rounds, then a breath */
	m->anim = ANIM_ATTACK; m->animt0 = game_ticks;
	snd_play_at(gun_sound[g], m->x, m->y, m->z);
	{   /* hit or miss, as a person would: a new target takes a moment to settle on, a moving one is harder, a far one
	       harder still, and a bot that is being hit flinches.  A miss goes past the body, not just off its middle */
		int p = bot_hit[difficulty], st = game_ticks - m->aimt, mv, side;
		if (st < bot_settle[difficulty]) p = p * (40 + 60 * st / bot_settle[difficulty]) / 100;
		mv = m->enemy < 0 ? pl_speed : m->enemy == link_P ? link_other_speed() : (mons[m->enemy].moving ? 60 : 0);   /* FX a tick */
		if (mv > 20) p = p * 55 / 100;
		if (du > 8) p = p * (100 - (du - 8) * 100 / 45) / 100;
		if (m->painflash) p = p * 60 / 100;
		if (g == GUN_CG) p = p * 70 / 100;                                                  /* spraying */
		if (p < 5) p = 5;
		err = d * 4 / 100 + 30;                                                              /* on target: a little scatter */
		tx = x + rnd(2 * err + 1) - err; ty = y + rnd(2 * err + 1) - err; tz = z + rnd(err + 1) - err / 2;
		if (rnd(100) >= p) {                                                                 /* a miss: wide, to one side or over */
			side = (PRAD * 3 / 2 + rnd(PRAD + d / 30)) * (rnd(2) ? 1 : -1);
			{ int yw = yaw_of(x - fx, y - fy); tx += (icos(yw) * side) >> 12; ty += (isin(yw) * side) >> 12; }
			if (rnd(3) == 0) tz += 300 + rnd(300);
		}
	}
	if (g == GUN_RL) {                                       /* leads its rocket at the chest, to burst there or on the way */
		launch(fx, fy, fz, (tx - fx) * 2, (ty - fy) * 2, (tz - fz) * 2, guns[g].speed, g, mi);
		return;
	}
	{   /* hitscan: the line on to the gun's range (no wall test past the target: it could see it) */
		int range = g == GUN_FIST ? 3 * FX : 48 * FX, dd = vlen3(tx - fx, ty - fy, tz - fz) + 1;
		int ex = fx + (tx - fx) * range / dd, ey = fy + (ty - fy) * range / dd, ez = fz + (tz - fz) * range / dd;
		if (g == GUN_SG) {
			int n, f = range / 40;
			for (n = 0; n < 20; n++) ray_damage(mi, fx, fy, fz, ex + rnd(2 * f + 1) - f, ey + rnd(2 * f + 1) - f, ez + rnd(2 * f + 1) - f, guns[g].dmg);
		} else ray_damage(mi, fx, fy, fz, ex, ey, ez, guns[g].dmg);
	}
}

/* ---- thinking ---- */
static int bot_goal_field(int mi) {
	Monster *m = &mons[mi]; int t = m->team, o = 1 - t;
	if (game_mode == GM_CTF) {
		if (flags[o].state == 1 && flags[o].carrier == mi) return t;                       /* got theirs: home */
		if (flags[t].state == 1) {                                                          /* ours stolen: hunt */
			nav_goal_at(2 + t, flags[t].x, flags[t].y); return 2 + t; }
		if (flags[t].state == 2 && (m->role || flags[o].state != 2)) { nav_goal_at(2 + t, flags[t].x, flags[t].y); return 2 + t; }
		if (flags[o].state == 2) { nav_goal_at(2 + t, flags[o].x, flags[o].y); return 2 + t; }   /* theirs lying about */
		if (flags[o].state == 1) return t;                                                  /* escort our carrier home */
		return m->role ? t : o;                                                             /* defend / attack */
	}
	if (game_mode == GM_KOTH && hill.on) return 0;                                         /* the hill, all of them */
	/* deathmatch: roam between two spots, or go after the human */
	if (m->role == 2 && !pl.dead && foes(mi, -1)) { nav_goal_at(2, pl.x, pl.y); return 2; }
	return m->goal & 1;
}

static void dm_reroam(int f) {                      /* a new roaming spot: an item somewhere */
	int k;
	for (k = 0; k < 20; k++) {
		int i = rnd(nitems ? nitems : 1);
		if (!nitems) break;
		if (nav_ok(items[i].x >> 8, items[i].y >> 8)) { nav_build(f, items[i].x >> 8, items[i].y >> 8); return; }
	}
}

static void bot_think(int mi) {
	Monster *m = &mons[mi]; int f, dir, x, y, z, a, d;
	if (m->gunwait > 0) m->gunwait--;
	if (m->painflash > 0) m->painflash--;
	if (!m->alive) { if (m->respawn && game_ticks >= m->respawn && !match_over) bot_spawn(mi); return; }
	if (((game_ticks + mi * 7) & 7) == 0) bot_pick_enemy(mi);
	if (m->enemy >= -1) { who_pos(m->enemy, &x, &y, &z, &a); if (!a) m->enemy = -2; }

	/* where to: downhill on the goal's map */
	f = bot_goal_field(mi);
	if (nav_on && game_mode != GM_CTF && game_mode != GM_KOTH && f < 2 && (unsigned)(m->x >> 8) < NAVN && (unsigned)(m->y >> 8) < NAVN && navf[f][(m->y >> 8) * NAVN + (m->x >> 8)] <= 2) {      /* reached the roaming spot */
		m->goal ^= 1; if (!rnd(3)) m->role = 2; else m->role = 0;
		dm_reroam(f);
	}
	dir = nav_dir(f, m->x, m->y);
	if (nav_was_drop && game_ticks >= m->lockt) { m->lockyaw = dir; m->lockt = game_ticks + 40; }   /* stepping off: go through with it */
	if (game_ticks < m->lockt) dir = m->lockyaw;
	m->move = BOT_RUN;
	if (game_mode == GM_CTF && m->role && f == m->team && dir >= 0 && navf[f][(m->y >> 8) * NAVN + (m->x >> 8)] < 8) dir = -1;   /* a defender near home: patrol */
	if (dir < 0) {                                  /* there, or no way from here: stroll about, look about */
		if (((game_ticks + mi * 31) % 100) == 0) m->myaw = rnd(4096);
		m->move = (game_ticks + mi * 13) % 160 < 70 ? BOT_WALK : 0;
		if (m->move && navf[f][(m->y >> 8) * NAVN + (m->x >> 8)] >= 8 && (dir = nav_dir(f, m->x, m->y)) >= 0) m->myaw = dir;   /* strayed: back */
	} else m->myaw = dir;

	/* stuck? (not a body length in a second and a half): jump and try sideways */
	if (game_ticks - m->stuckt > 90) {
		if (iabs(m->x - m->sx) + iabs(m->y - m->sy) < FX) { m->jumpnext = 1; m->myaw = (m->myaw + 1024 + rnd(2048)) & 4095; }
		m->stuckt = game_ticks; m->sx = m->x; m->sy = m->y;
	}

	/* face: the enemy, else where it goes; shoot when lined up */
	if (m->enemy >= -1) {
		m->targetyaw = yaw_of(x - m->x, y - m->y);
		d = ((m->targetyaw - m->yaw + 2048) & 4095) - 2048;
		if (m->gunwait <= 0 && iabs(d) < 160 && game_ticks >= m->think) bot_fire(mi);
		if (m->move) m->move = BOT_WALK;                 /* fighting: steadier, slower */
		if (dir >= 0 && game_mode != GM_CTF && m->role != 2) m->myaw = (dir + ((game_ticks / 120 + mi) & 1 ? 300 : -300)) & 4095;   /* a slow sidestep */
	} else m->targetyaw = m->myaw;
	d = ((m->targetyaw - m->yaw + 2048) & 4095) - 2048;      /* turn (Cube's monsters: 95 a tick) */
	if (d > 80) d = 80; if (d < -80) d = -80;
	m->yaw = (m->yaw + d) & 4095;

	monster_physics(mi);
	m->anim = m->moving ? ANIM_WALK : ANIM_IDLE;
	if (game_ticks - m->animt0 < 20 && m->gunwait > 0) m->anim = ANIM_ATTACK;
	bot_items(mi);
	if (m->z - m->eye < ABYSS_Z) {                   /* off the edge */
		m->alive = 0; m->anim = ANIM_DEATH; m->animt0 = game_ticks; m->deadvar = 0;
		bot_died(mi, mi);
	}
}

static void bot_teleports(void) {
	int i, j;
	for (i = 0; i < ntrigs; i++) {
		const Trig *t = &trigs[i];
		if (t->type != E_TELEPORT) continue;
		for (j = 0; j < nmons; j++) {
			Monster *m = &mons[j]; int dx, dy, dz, r = 4 * FX;
			if (m->bot != 1 || !m->alive) continue;
			dx = m->x - t->x; dy = m->y - t->y; dz = m->z - (t->z + m->eye);
			if (iabs(dx) >= r || iabs(dy) >= r || iabs(dz) >= r || dx * dx + dy * dy + dz * dz >= r * r) continue;
			if (teleport(t, &m->x, &m->y, &m->z, &m->yaw, m->eye, m->radius)) { m->myaw = m->targetyaw = m->yaw; m->vz = 0; snd_play_at(S_TELEPORT, m->x, m->y, m->z); }
		}
	}
}

/* teams for the bots: each joins the smaller side (red on a tie), the human (P1) being red and a second human (h1team,
   -1 = none) where it was put.  CTF jobs: each team keeps one defender per two opponents, but a team with no human
   always keeps somebody attacking; the humans count as attackers. */
static void qp_teams(int h1team) {
	int n[2] = { 1, 0 }, hum[2] = { 1, 0 }, k, t;
	if (h1team >= 0) n[h1team]++, hum[h1team]++;
	for (k = 0; k < nmons; k++) {
		Monster *m = &mons[k];
		if (m->bot != 1) continue;
		m->role = 0;
		if (game_mode == GM_DM) { m->team = k + 1; continue; }
		m->team = n[1] < n[0] ? 1 : 0; n[m->team]++;
	}
	if (game_mode != GM_CTF) return;
	for (t = 0; t < 2; t++) {
		int mine = n[t] - hum[t], def = (n[1 - t] + 1) / 2;
		if (def > mine - (hum[t] ? 0 : 1)) def = mine - (hum[t] ? 0 : 1);
		for (k = 0; k < nmons && def > 0; k++) if (mons[k].bot == 1 && mons[k].team == t) { mons[k].role = 1; def--; }
	}
}

void game_start_quick(int mapindex, int mode, int nb) {
	int i, k;
	game_start(mapindex);
	game_mode = mode; match_over = 0; team_score[0] = team_score[1] = 0; pl_frags = pl_caps = 0; pl_team = 0;
	match_limit = mode == GM_CTF ? 3 : mode == GM_TDM ? 20 : mode == GM_KOTH ? 60 : 15;
	world_settag(0, 0);                                     /* every door open: it is an arena now */
	nmons = 0; total_monsters = 0;                         /* the map's monsters stay home */
	nflags = 0;
	if (mode == GM_CTF)
		for (i = 0; i < world_nents(); i++) {
			const MapEnt *e = world_ent(i);
			if (e->type == E_FLAG && e->attr2 < 2) {
				Flag *f = &flags[e->attr2]; f->hx = e->x * FX; f->hy = e->y * FX; f->hz = world_cell(e->x, e->y)->floor * FX; flag_home(e->attr2);
				if (e->attr2 + 1 > nflags) nflags = e->attr2 + 1;
			}
		}
	hill.on = 0; hill.owner = -1; hill.acc[0] = hill.acc[1] = 0;
	if (mode == GM_KOTH)
		for (i = 0; i < world_nents(); i++) {
			const MapEnt *e = world_ent(i);
			if (e->type == E_HILL) { hill.on = 1; hill.x0 = e->x - e->attr1; hill.x1 = e->x + e->attr1; hill.y0 = e->y - e->attr2; hill.y1 = e->y + e->attr2; hill.z = e->z; }
		}
	nav_init();
	if (mode == GM_CTF && nflags == 2) { nav_build(0, flags[0].hx >> 8, flags[0].hy >> 8); nav_build(1, flags[1].hx >> 8, flags[1].hy >> 8); }
	else if (mode == GM_KOTH && hill.on) {                  /* the way to the hill: its open floor nearest the middle (cover may stand there) */
		int cx = (hill.x0 + hill.x1) / 2, cy = (hill.y0 + hill.y1) / 2, r, gx = cx, gy = cy, found = 0, dx, dy;
		for (r = 0; r < 8 && !found; r++) for (dy = -r; dy <= r && !found; dy++) for (dx = -r; dx <= r && !found; dx++)
			if (nav_ok(cx + dx, cy + dy) && world_cell(cx + dx, cy + dy)->floor == hill.z) { gx = cx + dx; gy = cy + dy; found = 1; }
		nav_build(0, gx, gy); dm_reroam(1);
	}
	else { dm_reroam(0); dm_reroam(1); }
	nbots = nb > 7 ? 7 : nb;
	for (k = 0; k < nbots; k++) {
		Monster *m = &mons[nmons++]; const MT *mt = &mtypes[0];
		memset(m, 0, sizeof(*m));
		m->bot = 1; m->type = 0; m->scale = mt->bscale; m->radius = PRAD * mt->bscale / 10; m->eye = 512 * mt->bscale / 10;
		/* teams: the human is red; bots alternate blue, red, ... so it is even when the bot count is odd */
		m->role = 0;
		m->goal = k;
		m->alive = 0; m->respawn = 0; m->animt0 = -100000;     /* in when the match starts */
	}
	qp_teams(-1);
	player_spawn_quick();
	qp_waiting = 1;                                        /* warm-up: run about the empty map; fire starts the match */
}

/* King of the Hill: a team alone on the hill scores a point a second; both there, nobody does */
static int on_hill(int x, int y, int feet) {
	return (x >> 8) >= hill.x0 && (x >> 8) < hill.x1 && (y >> 8) >= hill.y0 && (y >> 8) < hill.y1 && feet >= hill.z * FX - 128;
}
static void hill_update(void) {
	int in[2] = { 0, 0 }, i, o;
	if (!pl.dead && on_hill(pl.x, pl.y, pl.z - EYE)) in[pl_team]++;
	for (i = 0; i < nmons; i++) if (mons[i].bot && mons[i].alive && on_hill(mons[i].x, mons[i].y, mons[i].z - mons[i].eye)) in[mons[i].team]++;
	o = in[0] && in[1] ? 2 : in[0] ? 0 : in[1] ? 1 : -1;
	if (o != hill.owner) {
		if (o == 2) say_all("THE HILL IS CONTESTED");
		else if (o >= 0) { sprintf(qp_msg, "%s HOLDS THE HILL", team_name[o]); say_all(qp_msg); }
		else if (hill.owner >= 0) say_all("THE HILL IS EMPTY");
		hill.owner = o;
	}
	if (o == 0 || o == 1) {
		if (++hill.acc[o] >= 60) {
			hill.acc[o] = 0; team_score[o]++;
			if (team_score[o] >= match_limit) { sprintf(qp_msg, "%s TEAM TAKES THE HILL %d - %d", team_name[o], team_score[0], team_score[1]); match_end(qp_msg); }
		}
	}
}

static void quick_begin(void) {                           /* fire pressed in the warm-up: everyone in, at once */
	int k;
	qp_waiting = 0; pl_flying = 0;
	player_spawn_quick();
	for (k = 0; k < nmons; k++) if (mons[k].bot == 1) bot_spawn(k);
	spawn_t0 = game_ticks;
	say_all(game_mode == GM_CTF ? "CAPTURE THE FLAG: BRING THEIR FLAG TO YOURS" : game_mode == GM_KOTH ? "KING OF THE HILL: HOLD THE MIDDLE" : game_mode == GM_TDM ? "TEAM DEATHMATCH" : "DEATHMATCH");
}

void __attribute__((used, noipa)) game_quick_begin(void) { if (qp_waiting) quick_begin(); }      /* (tests: as if fire were pressed) */

static void quick_human(int pressed) {                  /* the human in pl: back in after a death, or lost in space */
	if (match_over || qp_waiting) return;
	if (pl.dead) {                                          /* back in after START, or 4 seconds */
		if (!pl_respawn_at) pl_respawn_at = game_ticks + 4 * 60;
		if ((pressed & PAD_START) || game_ticks >= pl_respawn_at) player_spawn_quick();
	} else if (pl.z - EYE < ABYSS_Z) { hurt_src = -2; hurt_player(1000); }
}
static void quick_world(void) {                         /* everyone else */
	int i;
	if (match_over || qp_waiting) return;                  /* warm-up: you fly (no-clip); nothing else runs */
	for (i = 0; i < nmons; i++) if (mons[i].bot == 1) bot_think(i);
	bot_teleports();
	items_respawn();
	if (game_mode == GM_CTF) flags_update();
	if (game_mode == GM_KOTH && hill.on) hill_update();
}
