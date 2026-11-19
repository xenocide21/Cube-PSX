/* Cube PSX game logic, ported from Cube's clientgame.cpp / monster.cpp / weapon.cpp / entities.cpp.
 * 60 ticks per second; Cube's millisecond timers run on a game clock (now_ms). */
#include "cube.h"
#include <stddef.h>
#include <psxgte.h>
#include <psxpad.h>
#include <string.h>
#include <stdio.h>

Player pl;
Monster mons[MAXMON]; int nmons;
Item items[MAXITEM];  int nitems;
Proj projs[MAXPROJ];
Fx fxs[MAXFX];
int game_msg_timer; const char *game_msg = "";
int total_monsters;
static int curmap;
Trig trigs[MAXTRIG]; int ntrigs;
int level_done;
static int level_t0;
int level_secs;
static char level_msg[192];
int difficulty = 1;                         /* 0 easy, 1 normal (the tuning played so far), 2 hard */
int demo_mode, demo_done;
/* difficulty: Cube's `skill` (1-10, its default 3) makes monsters decide faster (monster.cpp transition); on top of
   that monster damage is scaled. Normal = Cube's default skill. */
static const int16_t diff_skill[3] = { 1, 3, 7 }, diff_dmg[3] = { 60, 100, 140 };
static int mon_dmg(int d) { return (d * diff_dmg[difficulty] + 50) / 100; }
static int src_dmg(int src, int d);          /* d from src as it lands on a human: scaled unless a human fired it */
static uint8_t tag_open[32];                /* doors opened so far (for save games) */
static int quip_at, quip_id, quip_ready, last_quip = -1, last_grunt = -100, last_complain = -100000;
static uint8_t complained[32];


#define EYE      819          /* 3.2 units */
#define ABOVE     51
#define PRAD     281          /* 1.1 units */
#define STEPMAX  384
#define JUMPVZ  2553          /* Cube's jump, 1.7 x 22 u/s: 0.62 units a tick (x16) */
#define TICKMS    17
/* Options sliders, 1..10.  Move speed: 1 = the PS1 tuning played so far (40% of Cube's real running speed), 10 = Cube's
 * own speed; monsters scale with it so the balance holds.  Turn speed: 1 = the current turning, 10 = the full rate of
 * v0.2 (before it was slowed for the pad). */
int opt_move = 1, opt_turn = 1;
int opt_sfx = 8, opt_music = 8, opt_speech = 8;   /* volumes 0..10 */
int opt_look = 1, opt_invert = 0, opt_aim = 1;
int opt_char;                                     /* 0 Giga Chad, 1 Giga Stacy */
int automap_on;
#define SPEED_PCT (40 + (opt_move - 1) * 60 / 9)
#define TURN_PCT (40 + (opt_turn - 1) * 60 / 9)

static unsigned int rngs = 12345;
static int rnd(int n) { rngs = rngs * 1103515245u + 12345u; return (int)((rngs >> 16) & 0x7fff) % n; }

/* weapons: delay (ticks), damage, projectile speed (FX/tick, 0 = hitscan) */
typedef struct { int delay, dmg, speed; const char *name; } GunInfo;
static const GunInfo guns[NUMGUNS] = {
	{ 15,  50,   0, "FIST" },
	{ 84,  10,   0, "SHOTGUN" },
	{  6,  30,   0, "CHAINGUN" },
	{ 48, 120, 341, "ROCKETS" },
	{ 90, 100,   0, "RIFLE" },
	{ 12,  20, 213, "FIREBALL" },
	{ 12,  40, 128, "ICEBALL" },
	{ 12,  30, 683, "SLIMEBALL" },
	{ 15,  50,   0, "BITE" },
	{ 33,  22,   0, "PISTOL" },          /* ours: one bullet a shot (0.55 s), well under the shotgun */
};
const char *gun_name(int g) { return guns[g].name; }
static const uint8_t gun_sound[NUMGUNS] = { S_PUNCH1, S_SG, S_CG, S_RLFIRE, S_RIFLE, S_FLAUNCH, S_ICEBALL, S_SLIMEBALL, S_PIGR1, S_CG };

/* Cube's monstertypes: gun, speed (u/s), health, lag (ms from deciding to fire to firing), rate (ms between looks
   while closing in), pain (ms stunned when hit), loyalty (hits from another monster before turning on it), body scale */
typedef struct { int gun, speed, health, lag, rate, pain, loyalty, bscale; } MT;
static const MT mtypes[8] = {
	{ GUN_FIREBALL,  15, 100,   0, 100, 800, 1, 10 },   /* ogre */
	{ GUN_CG,        18,  70,  70,  10, 400, 2,  9 },   /* rhino */
	{ GUN_SG,        14, 120, 100, 300, 400, 4, 14 },   /* ratamahatta */
	{ GUN_RIFLE,     15, 200,  80, 300, 300, 4, 18 },   /* slith */
	{ GUN_RL,        13, 500,   0, 100, 200, 6, 24 },   /* bauul */
	{ GUN_BITE,      22,  50,   0, 100, 400, 1, 15 },   /* hellpig */
	{ GUN_ICEBALL,   12, 250,   0,  10, 400, 6, 18 },   /* knight */
	{ GUN_SLIMEBALL, 15, 100,   0, 200, 400, 2, 10 },   /* goblin */
};
static const uint8_t mon_painsnd[8] = { S_PAINO, S_PAINR, S_PAINE, S_PAINS, S_PAINB, S_PAINP, S_PAINH, S_PAIND };
static const uint8_t mon_diesnd[8] = { S_DIE1, S_DEATHR, S_DEATHE, S_DEATHS, S_DEATHB, S_PIGGR2, S_DEATHH, S_DEATHD };
#define MONSTERDAMAGEFACTOR 4                 /* monsters do a quarter of a weapon's damage (weapon.cpp) */
#define now_ms (game_ticks * 50 / 3)
/* how far bots and monsters can see you: your own view distance - except in a link game, where both consoles must agree */
#define LINK_SIGHT 24
static int sight_cells(void) { return link_on ? LINK_SIGHT : render_view_cells(); }
#define UNIT FX
static int mabove(const Monster *m) { return 486 * m->scale / 10; }      /* 1.9 units above the eye, scaled */

int mon_color(int t, int *r, int *g, int *b) {
	static const unsigned char c[8][3] = { {200,90,40},{150,150,160},{190,150,90},{90,170,90},{170,60,60},{220,120,150},{110,130,220},{120,220,80} };
	*r = c[t & 7][0]; *g = c[t & 7][1]; *b = c[t & 7][2]; return t;
}

int link_on, link_me, link_phase, game_msg_for = -1;
static int link_P = -1000;                   /* link play: the proxy's monster slot (the human who is not in pl) */
static void say(const char *m) { game_msg = m; game_msg_timer = 150; game_msg_for = link_on ? link_phase : -1; }   /* to whoever is in pl */
static void say_all(const char *m) { game_msg = m; game_msg_timer = 150; game_msg_for = -1; }

static void add_fx(int x, int y, int z) {
	int i;
	for (i = 0; i < MAXFX; i++) if (fxs[i].life <= 0) { fxs[i].x = x; fxs[i].y = y; fxs[i].z = z; fxs[i].life = 14; return; }
}

static void player_respawn(void) {
	int i, n = 0, pick, sx = 64 * FX, sy = 64 * FX, sa = 0;
	for (i = 0; i < world_nents(); i++) if (world_ent(i)->type == E_PLAYERSTART) n++;
	if (n) {
		pick = rnd(n);
		for (i = 0; i < world_nents(); i++) {
			const MapEnt *e = world_ent(i);
			if (e->type != E_PLAYERSTART) continue;
			if (pick-- == 0) { sx = e->x * FX + 128; sy = e->y * FX + 128; sa = e->attr1; break; }
		}
	}
	memset(&pl.ammo, 0, sizeof(pl.ammo));
	pl.x = sx; pl.y = sy;
	{ const Cell *c = world_cell(sx >> 8, sy >> 8); pl.z = c->floor * FX + EYE; }
	pl.vx = pl.vy = pl.vz = 0;
	pl.yaw = (sa * 4096 / 360) & 4095; pl.pitch = 0;
	pl.health = 100; pl.armour = 50; pl.armourtype = 0;
	pl.gun = GUN_SG; pl.gunwait = 0; pl.fireflash = 0;
	pl.ammo[GUN_FIST] = 1; pl.ammo[GUN_SG] = 20; pl.ammo[GUN_CG] = 40;
	pl.dead = 0; pl.hurt = 0; pl.onfloor = 0;
	memset(projs, 0, sizeof(projs));
}

void game_start(int mapindex) {
	int i;
	curmap = mapindex % NUM_MAPS; automap_on = 0; game_mode = GM_SP; match_over = 0; nflags = 0; qp_waiting = 0; pl_flying = 0;
	world_load(curmap);
	nmons = 0; nitems = 0; ntrigs = 0; total_monsters = 0; level_done = 0; level_t0 = game_ticks;
	world_settag(0, 1);                   /* Cube's settagareas: every door (tagged area) starts closed */
	memset(tag_open, 0, sizeof(tag_open));
	demo_mode = 0; demo_done = 0;
	memset(fxs, 0, sizeof(fxs));
	pl.kills = 0; pl.deaths = 0;
	memset(complained, 0, sizeof(complained)); quip_at = 0; quip_ready = 0;
	for (i = 0; i < world_nents(); i++) {
		const MapEnt *e = world_ent(i);
		if (e->type == E_MONSTER && nmons < MAXMON) {
			Monster *m = &mons[nmons++];
			int t = e->attr2 % 8; const MT *mt = &mtypes[t];
			const Cell *c = world_cell(e->x, e->y);
			memset(m, 0, sizeof(*m));
			m->type = t; m->health = mt->health; m->scale = mt->bscale;           /* Cube basicmonster(): M_SLEEP */
			m->radius = PRAD * mt->bscale / 10; m->eye = 512 * mt->bscale / 10;
			m->x = e->x * FX + 128; m->y = e->y * FX + 128; m->z = c->floor * FX + m->eye;
			m->yaw = m->targetyaw = (e->attr1 * 4096 / 360) & 4095;
			m->mstate = M_SLEEP; m->trigger = 100; m->enemy = -1; m->aware = 0; m->onfloor = 1; m->alive = 1; m->anim = ANIM_IDLE;
			total_monsters++;
		} else if ((e->type == E_CARROT || e->type == E_TELEPORT || e->type == E_JUMPPAD) && ntrigs < MAXTRIG) {
			Trig *t = &trigs[ntrigs++];
			t->x = e->x * FX; t->y = e->y * FX; t->z = world_cell(e->x, e->y)->floor * FX;
			t->type = e->type; t->attr1 = e->attr1; t->attr2 = e->attr2; t->attr3 = e->attr3; t->attr4 = e->attr4; t->spawned = 1;
		} else if (((e->type >= E_SHELLS && e->type <= E_YELLOWARMOUR) || e->type == E_CLIPS) && nitems < MAXITEM) {
			Item *it = &items[nitems++];
			it->x = e->x * FX + 128; it->y = e->y * FX + 128;
			it->z = world_cell(e->x, e->y)->floor * FX;
			it->type = e->type; it->taken = 0;
		}
	}
	player_respawn();
	say(world_title());
}

/* ------------- damage ------------- */
int game_ticks = 0;
int gun_delay(int g) { return guns[g].delay; }

/* yaw (4096 = turn) that faces direction (dx,dy): forward = (-sin yaw, cos yaw) */
static int yaw_of(int dx, int dy);
int yaw_to(int dx, int dy) { return yaw_of(dx, dy); }
static int yaw_of(int dx, int dy) {
	int ax = iabs(dx), ay = iabs(dy), t, a, u;
	if (ax == 0 && ay == 0) return 0;
	if (ay <= ax) { t = ay * 256 / ax; u = t * (t - 256); a = 2 * t - ((u * (160 + (43 * t >> 8))) >> 16); }
	else { t = ax * 256 / ay; u = t * (t - 256); a = 1024 - (2 * t - ((u * (160 + (43 * t >> 8))) >> 16)); }
	if (dx < 0) a = 2048 - a;
	if (dy < 0) a = -a;
	return (a - 1024) & 4095;
}

/* the dude's voice: kill quips (about one kill in three, at least 4 s apart, never the same line twice running, said just
   after the monster's death cry), grunts when hit, a complaint at a locked door */
static void kill_quip(int dmg) {
	static const uint8_t lines[6] = { V_OHYEAH, V_EASY, V_HURT, V_NEXT, V_STAYDOWN, V_TOOSLOW };
	int id;
	if (demo_mode || game_ticks < quip_ready || quip_at || rnd(3)) return;
	do id = (dmg >= 100 && rnd(2)) ? V_HURT : lines[rnd(6)]; while (id == last_quip);
	quip_id = last_quip = id; quip_at = game_ticks + 24; quip_ready = game_ticks + 240;
}

#ifdef CUBE_HOST
static void hurt_player(int dmg);
void hurt_player_test(int dmg) { hurt_player(dmg); }
#endif
static int pl_speed, pl_lx0, pl_ly0;         /* how far the player moved last tick (FX): bots miss a moving target more */
static int hurt_src = -1000;                 /* who is doing the damage (-1 player, >= 0 monster / bot, -2 falling) */
static int foes(int a, int b);
static void bot_died(int mi, int attacker);
static void link_pend(int dmg, int attacker);
static void frag(int killer, int victim);
static void hurt_player(int dmg) {
	int ad, src = hurt_src;
	hurt_src = -1000;
	if (pl.dead || demo_mode) return;
	if (game_mode != GM_SP && src >= 0 && mons[src].bot && !foes(src, -1)) return;        /* no friendly fire */
	ad = dmg * (pl.armourtype + 1) * 20 / 100;
	if (ad > pl.armour) ad = pl.armour;
	pl.armour -= ad; pl.health -= dmg - ad; pl.hurt = 18;
	if (pl.health <= 0) {
		pl.health = 0; pl.dead = 1; pl.deaths++; snd_say(V_DIE, 1); quip_at = 0;
		if (game_mode == GM_SP) say("YOU DIED - PRESS START");
		else { say(src == -2 ? "YOU FELL INTO SPACE" : "FRAGGED - BACK IN A MOMENT"); frag(src >= 0 ? src : src == -1 ? -1 : -2, -1); }
	}
	else if (game_ticks - last_grunt >= 24) {             /* the dude grunts (not more than every 0.4 s); talking, Cube's pain sound */
		last_grunt = game_ticks;
		if (!snd_say(V_GRUNT1 + rnd(3), 0)) snd_play(S_PAIN6);
	}
}

/* ---- Cube's monster AI is sequenced with transitions: a monster stays in a state until its trigger time, then
   re-evaluates.  n = ms at skill 0 (n/2 at skill 10), r = random ms added. ---- */
static void transition(Monster *m, int state, int moving, int n, int r) {
	int sk = diff_skill[difficulty];
	m->mstate = state; m->move = moving;
	n = n * 130 / 100;
	m->trigger = now_ms + n - sk * (n / 16) + rnd(r + 1);
	if (state == M_ATTACKING) { m->anim = ANIM_ATTACK; m->animt0 = game_ticks; }
	if (state == M_PAIN) { m->anim = ANIM_PAIN; m->animt0 = game_ticks; }
}

/* attacker: -1 = the player, else the monster index that did it (monsterpain) */
static void monster_pain(Monster *m, int dmg, int attacker) {
	const MT *mt = &mtypes[m->type];
	if (!m->alive) return;
	if (m->bot) {                                          /* a bot: no anger, no pain pause; teammates cannot hurt it */
		int mi = m - mons;
		if (attacker != mi && !foes(attacker, mi)) return;
		if (mi == link_P) { link_pend(dmg, attacker); return; }    /* the other human: it lands when they are in pl */
		m->painflash = 8;
		if ((m->health -= dmg) <= 0) {
			snd_play_at(S_DIE1 + rnd(2), m->x, m->y, m->z);
			m->anim = ANIM_DEATH; m->animt0 = game_ticks; m->deadvar = (m->x + m->y + game_ticks) / 7 % 3; m->alive = 0;
			bot_died(mi, attacker);
			if (attacker < 0) kill_quip(dmg);
		} else snd_play_at(S_PAINO, m->x, m->y, m->z);
		return;
	}
	if (attacker >= 0) {                                   /* a monster hit us */
		if (&mons[attacker] != m) {                         /* guard for rocket guys shooting themselves */
			int anger = ++m->anger;                          /* don't attack straight away, first get angry */
			if (mons[attacker].type == m->type) anger /= 2;
			if (anger >= mt->loyalty) m->enemy = attacker;   /* monsters infight if very angry */
		}
	} else { m->anger = 0; m->enemy = -1; }
	transition(m, M_PAIN, 0, mt->pain, 200);               /* in this state a monster won't attack */
	m->painflash = 8;
	if ((m->health -= dmg) <= 0) {
		snd_play_at(mon_diesnd[m->type], m->x, m->y, m->z);
		m->anim = ANIM_DEATH; m->animt0 = game_ticks; m->deadvar = (m->x + m->y + game_ticks) / 7 % 3; m->alive = 0;
		pl.kills++;                                        /* Cube counts every monster killed, by anyone */
		if (attacker < 0) kill_quip(dmg);
	} else snd_play_at(mon_painsnd[m->type], m->x, m->y, m->z);
}

/* rocket splash (weapon.cpp radialeffect): everything within 7 units, damage falling off with distance; the owner too */
static void explode(int x, int y, int z, int owner, int qdam) {
	int i, r = 7 * FX, d;
	add_fx(x, y, z);
	snd_play_at(S_RLHIT, x, y, z);
	for (i = 0; i < nmons; i++) {
		Monster *m = &mons[i]; int dx, dy, dz;
		if (!m->alive) continue;
		dx = m->x - x; dy = m->y - y; dz = m->z - z;
		d = vlen3(dx, dy, dz) - 2 * FX; if (d < 0) d = 0;
		if (d < r) monster_pain(m, qdam * (r - d) / r, owner);
	}
	if (!pl.dead) {
		int dx = pl.x - x, dy = pl.y - y, dz = pl.z - z;
		d = vlen3(dx, dy, dz) - 2 * FX; if (d < 0) d = 0;
		if (d < r) { hurt_src = owner; hurt_player(src_dmg(owner, qdam) * (r - d) / r); }
	}
}

/* ------------- shooting ------------- */
/* fire a projectile from (x,y,z) over the vector (dx,dy,dz) at sp FX per tick */
static void launch(int x, int y, int z, int dx, int dy, int dz, int sp, int gun, int owner) {
	int i, len = vlen3((dx >> 4), (dy >> 4), (dz >> 4)) << 4;
	if (sp < 1) sp = 1;
	for (i = 0; i < MAXPROJ; i++) if (!projs[i].life) {
		Proj *p = &projs[i];
		p->x = p->sx = x; p->y = p->sy = y; p->z = p->sz = z; p->dx = dx; p->dy = dy; p->dz = dz;
		p->step = 0; p->steps = len * 3 / sp + 1; p->gun = gun; p->owner = owner; p->life = 1;
		return;
	}
}


static int ray_monster(int ox, int oy, int oz, int dx, int dy, int dz, int maxd, int *which) {
	int i, best = maxd; *which = -1;
	for (i = 0; i < nmons; i++) {
		Monster *m = &mons[i];
		int cx, cy, cz, t, px, py, pz, hx, hy, feet, hgt;
		if (!m->alive) continue;
		feet = m->z - m->eye; hgt = m->eye + (ABOVE * m->scale / 10 + 400);
		cx = m->x - ox; cy = m->y - oy; cz = (feet + hgt / 2) - oz;
		t = (cx * dx + cy * dy + cz * dz) >> 12;
		if (t < 0 || t >= best) continue;
		px = ox + ((dx * t) >> 12); py = oy + ((dy * t) >> 12); pz = oz + ((dz * t) >> 12);
		hx = px - m->x; hy = py - m->y;
		if (iabs(hx) > m->radius || iabs(hy) > m->radius || hx * hx + hy * hy > m->radius * m->radius) continue;
		if (pz < feet || pz > feet + hgt) continue;
		best = t; *which = i;
	}
	return best;
}

static void aim_dir(int yaw, int pitch, int *dx, int *dy, int *dz) {
	int cp = icos(pitch);
	*dx = (-isin(yaw) * cp) >> 12; *dy = (icos(yaw) * cp) >> 12; *dz = -isin(pitch);
}

/* Doom-style auto-aim: a live monster within ~10 degrees of the crosshair (any height) is aimed at */
static void autoaim(int *dx, int *dy, int *dz) {
	int fx = *dx, fy = *dy, i, best = 0x7fffffff, bi = -1;
	int hl = vlen2(fx, fy);
	if (hl < 64) return;
	for (i = 0; i < nmons; i++) {
		Monster *m = &mons[i]; int tx, ty, tz, fw, lat, d, wd;
		if (!m->alive || (game_mode != GM_SP && !foes(-1, i))) continue;
		tx = m->x - pl.x; ty = m->y - pl.y; tz = (m->z - m->eye / 2) - pl.z;
		fw = (fx * tx + fy * ty) / hl; lat = (fx * ty - fy * tx) / hl;
		if (fw <= 0 || fw > 40 * FX) continue;
		if (lat < 0) lat = -lat;
		if (lat * 6 > fw + m->radius * 6) continue;
		d = vlen3(tx, ty, tz);
		if (d >= best || d < 1) continue;
		wd = world_ray(pl.x, pl.y, pl.z, tx * 4096 / d, ty * 4096 / d, tz * 4096 / d, d);
		if (wd < d - m->radius - 16) continue;      /* wall in between */
		best = d; bi = i;
	}
	if (bi >= 0) {
		Monster *m = &mons[bi]; int tx = m->x - pl.x, ty = m->y - pl.y, tz = (m->z - m->eye / 2) - pl.z, d = vlen3(tx, ty, tz);
		*dx = tx * 4096 / d; *dy = ty * 4096 / d; *dz = tz * 4096 / d;
	}
}

static void fire_player(void) {
	int dx, dy, dz, n, rays, g = pl.gun, i;
	if (pl.gunwait > 0 || pl.dead) return;
	if (pl.ammo[g] <= 0) return;
	if (g != GUN_FIST) pl.ammo[g]--;
	pl.gunwait = guns[g].delay; pl.fireflash = 4;
	snd_play(gun_sound[g]);
	aim_dir(pl.yaw, pl.pitch, &dx, &dy, &dz);
	if (opt_aim) autoaim(&dx, &dy, &dz);
	if (g == GUN_RL) {
		launch(pl.x + (dx >> 4), pl.y + (dy >> 4), pl.z - 60, dx * 10, dy * 10, dz * 10, guns[g].speed, g, -1);    /* up to 160 units out */
		return;
	}
	rays = (g == GUN_SG) ? 20 : 1;
	{
		static int16_t acc[MAXMON]; int hit[20], nh = 0;
		for (n = 0; n < rays; n++) {
			int rx = dx, ry = dy, rz = dz, which, wd, range = (g == GUN_FIST) ? 3 * FX : 48 * FX;
			if (g == GUN_SG) { rx += rnd(801) - 400; ry += rnd(801) - 400; rz += rnd(801) - 400; }
			else if (g == GUN_CG || g == GUN_PISTOL) { rx += rnd(121) - 60; ry += rnd(121) - 60; rz += rnd(121) - 60; }
			wd = world_ray(pl.x, pl.y, pl.z, rx, ry, rz, range);
			ray_monster(pl.x, pl.y, pl.z, rx, ry, rz, wd, &which);
			if (which >= 0) { if (!acc[which]) hit[nh++] = which; acc[which] += guns[g].dmg; }
		}
		/* a shotgun blast lands as one hit per monster, all its pellets added up (weapon.cpp raydamage) */
		for (n = 0; n < nh; n++) { monster_pain(&mons[hit[n]], acc[hit[n]], -1); acc[hit[n]] = 0; }
	}
}

/* Does the segment a-b pass through the box of something standing with its eye at (ox,oy,oz)?  (weapon.cpp intersect) */
static int seg_hits(int ax, int ay, int az, int bx, int by, int bz, int ox, int oy, int oz, int rad, int eye, int above) {
	int vx = bx - ax, vy = by - ay, vz = bz - az, wx = ox - ax, wy = oy - ay, wz = oz - az;
	int c1 = (vx >> 4) * (wx >> 4) + (vy >> 4) * (wy >> 4) + (vz >> 4) * (wz >> 4);
	int c2 = (vx >> 4) * (vx >> 4) + (vy >> 4) * (vy >> 4) + (vz >> 4) * (vz >> 4);
	int px, py, pz;
	if (c1 <= 0) { px = ax; py = ay; pz = az; }
	else if (c2 <= c1) { px = bx; py = by; pz = bz; }
	else { int f = c1 / ((c2 >> 12) + 1); if (f > 4096) f = 4096; px = ax + (vx * f >> 12); py = ay + (vy * f >> 12); pz = az + (vz * f >> 12); }
	return px <= ox + rad && px >= ox - rad && py <= oy + rad && py >= oy - rad && pz >= oz - eye && pz <= oz + above;
}

/* hitscan damage from monster m along from->to to everything in the way: the player and the other monsters, like
   Cube's raydamage (no wall test: the monster only fires at a point it could see) */
static void ray_damage(int mi, int fx, int fy, int fz, int tx, int ty, int tz, int qdam) {
	int i;
	if (!pl.dead && seg_hits(fx, fy, fz, tx, ty, tz, pl.x, pl.y, pl.z, PRAD, EYE, ABOVE)) { hurt_src = mi; hurt_player(mon_dmg(qdam)); }
	for (i = 0; i < nmons; i++) {
		Monster *o = &mons[i];
		if (i == mi || !o->alive) continue;
		if (seg_hits(fx, fy, fz, tx, ty, tz, o->x, o->y, o->z, o->radius, o->eye, mabove(o))) monster_pain(o, qdam, mi);
	}
}

/* weapon.cpp shoot() for a monster: fire at the point it locked on to when it started aiming */
static void monster_shoot(int mi) {
	Monster *m = &mons[mi]; const MT *mt = &mtypes[m->type];
	int g = mt->gun, qdam = guns[g].dmg / MONSTERDAMAGEFACTOR, i;
	int fx = m->x, fy = m->y, fz = m->z - 51, tx = m->atx, ty = m->aty, tz = m->atz;     /* from just below the eye */
	int dx = tx - fx, dy = ty - fy, dz = tz - fz, d = vlen3(dx, dy, dz);
	if (d < 1) return;
	snd_play_at(gun_sound[g], m->x, m->y, m->z);
	if (g == GUN_BITE || g == GUN_FIST) {                   /* reach: 3 units toward the target */
		tx = fx + dx * (3 * FX) / d; ty = fy + dy * (3 * FX) / d; tz = fz + dz * (3 * FX) / d;
	}
	if (guns[g].speed)                                      /* projectile at half speed, bursting where it was aimed */
		launch(fx, fy, fz, dx, dy, dz, guns[g].speed / 2 * SPEED_PCT / 100, g, mi);
	else if (g == GUN_SG) {                               /* 20 rays spread around the target, 2% of the distance */
		int n, f = d * 2 / 1000, rx[20], ry[20], rz[20], i, hits;
		for (n = 0; n < 20; n++) { rx[n] = tx + (rnd(101) - 50) * f; ry[n] = ty + (rnd(101) - 50) * f; rz[n] = tz + (rnd(101) - 50) * f; }
		/* all the pellets that hit one target count as one hit (weapon.cpp raydamage) */
		for (hits = 0, n = 0; n < 20; n++) if (!pl.dead && seg_hits(fx, fy, fz, rx[n], ry[n], rz[n], pl.x, pl.y, pl.z, PRAD, EYE, ABOVE)) hits++;
		if (hits) { hurt_src = mi; hurt_player(mon_dmg(qdam * hits)); }
		for (i = 0; i < nmons; i++) {
			Monster *o = &mons[i];
			if (i == mi || !o->alive) continue;
			for (hits = 0, n = 0; n < 20; n++) if (seg_hits(fx, fy, fz, rx[n], ry[n], rz[n], o->x, o->y, o->z, o->radius, o->eye, mabove(o))) hits++;
			if (hits) monster_pain(o, qdam * hits, mi);
		}
	} else ray_damage(mi, fx, fy, fz, tx, ty, tz, qdam);
}

/* ------------- per-tick updates ------------- */
static void update_projs(void) {
	int i, k;
	for (i = 0; i < MAXPROJ; i++) {
		Proj *p = &projs[i];
		int qdam;
		if (!p->life) continue;
		qdam = p->owner < 0 || mons[p->owner].bot ? guns[p->gun].dmg : guns[p->gun].dmg / MONSTERDAMAGEFACTOR;
		for (k = 0; k < 3 && p->life; k++) {
			const Cell *c; int j, hit = -2, px = p->x, py = p->y, pz = p->z;
			p->step++;
			p->x = p->sx + p->dx / p->steps * p->step + p->dx % p->steps * p->step / p->steps;
			p->y = p->sy + p->dy / p->steps * p->step + p->dy % p->steps * p->step / p->steps;
			p->z = p->sz + p->dz / p->steps * p->step + p->dz % p->steps * p->step / p->steps;
			c = world_cell(p->x >> 8, p->y >> 8);
			if (c->type == SOLID_T || p->z < c->floor * FX || p->z > c->ceil * FX) {
				if (p->gun == GUN_RL) explode(px, py, pz, p->owner, qdam); else { add_fx(px, py, pz); snd_play_at(S_FEXPLODE, px, py, pz); }
				p->life = 0; break;
			}
			/* anything but its owner can be hit (projdamage) */
			if (p->owner >= 0 && !pl.dead) {
				int hx = p->x - pl.x, hy = p->y - pl.y;
				if (iabs(hx) < PRAD && iabs(hy) < PRAD && hx * hx + hy * hy < PRAD * PRAD && p->z >= pl.z - EYE && p->z <= pl.z + ABOVE) hit = -1;
			}
			for (j = 0; j < nmons && hit == -2; j++) {
				Monster *m = &mons[j]; int hx = p->x - m->x, hy = p->y - m->y;
				if (!m->alive || j == p->owner) continue;
				if (iabs(hx) < m->radius && iabs(hy) < m->radius && hx * hx + hy * hy < m->radius * m->radius && p->z >= m->z - m->eye && p->z <= m->z + mabove(m)) hit = j;
			}
			if (hit != -2) {
				if (p->gun == GUN_RL) explode(p->x, p->y, p->z, p->owner, qdam);
				else if (hit == -1) { hurt_src = p->owner; hurt_player(src_dmg(p->owner, qdam)); snd_play_at(S_FEXPLODE, p->x, p->y, p->z); }
				else { monster_pain(&mons[hit], qdam, p->owner); snd_play_at(S_FEXPLODE, p->x, p->y, p->z); }
				p->life = 0; break;
			}
			if (p->step >= p->steps) {                         /* reached the point it was fired at: burst */
				if (p->gun == GUN_RL) explode(p->x, p->y, p->z, p->owner, qdam); else { add_fx(p->x, p->y, p->z); snd_play_at(S_FEXPLODE, p->x, p->y, p->z); }
				p->life = 0;
			}
		}
	}
}

static void enemy_pos(const Monster *m, int *x, int *y, int *z, int *alive) {
	if (m->enemy < 0) { *x = pl.x; *y = pl.y; *z = pl.z; *alive = !pl.dead; }
	else { const Monster *e = &mons[m->enemy]; *x = e->x; *y = e->y; *z = e->z; *alive = e->alive; }
}

/* in water: Cube's test, waterlevel > eye - 0.5 */
static int in_water(int eyez) { int w = world_water(); return w != NO_WATER && w * FX > eyez - 128; }

/* ---- monster movement (physics.cpp moveplayer for a monster) ---- */
/* bodies are boxes (physics.cpp plcollide); a move into another body's box is blocked, but only if it brings the two
   closer, so nothing can get stuck inside something it was already overlapping */
static int body_blocks(int fx, int fy, int tx, int ty, int r, int ox, int oy) {
	int ndx = iabs(ox - tx), ndy = iabs(oy - ty);
	if (ndx >= r || ndy >= r) return 0;
	return (ndx > ndy ? ndx : ndy) < (iabs(ox - fx) > iabs(oy - fy) ? iabs(ox - fx) : iabs(oy - fy));
}

/* Can monster mi stand at (x,y)?  World, the "big monsters are afraid of heights" rule, and bodies in the way. */
static int monster_fits(int mi, int x, int y, int *fz) {
	Monster *m = &mons[mi]; int cz, feet = m->z - m->eye, j, r;
	if (!world_fits(x, y, m->radius, feet, STEPMAX, fz, &cz)) return 0;
	if (m->health > 100 && m->onfloor && *fz < feet - 1152) return 0;          /* 4.5 units */
	if (!pl.dead && body_blocks(m->x, m->y, x, y, m->radius + PRAD, pl.x, pl.y) && iabs(pl.z - m->z) < ABOVE + m->eye) return 0;
	for (j = 0; j < nmons; j++) {
		Monster *o = &mons[j];
		if (j == mi || !o->alive) continue;
		r = m->radius + o->radius;
		if (body_blocks(m->x, m->y, x, y, r, o->x, o->y) && iabs(o->z - m->z) < mabove(o) + m->eye) return 0;
	}
	return 1;
}

/* is the player's move to (x,y) blocked by a monster? */
static int player_blocked(int x, int y) {
	int j;
	for (j = 0; j < nmons; j++) {
		Monster *o = &mons[j];
		if (o->alive && body_blocks(pl.x, pl.y, x, y, PRAD + o->radius, o->x, o->y) && iabs(o->z - pl.z) < mabove(o) + EYE) return 1;
	}
	return 0;
}

static void monster_physics(int mi) {
	Monster *m = &mons[mi]; const MT *mt = &mtypes[m->type];
	int fz, cz, feet, ox = m->x, oy = m->y;
	int wet = in_water(m->z);
	if (m->move) {
		int step = (m->bot ? 102 * SPEED_PCT / 100 * m->move / 100 : mt->speed * FX * SPEED_PCT / 6000) / (wet ? 2 : 1), yw = m->bot ? m->myaw : m->yaw;   /* (a bot's move = % of a player's run) */
		int nx = m->x + ((-isin(yw) * step) >> 12), ny = m->y + ((icos(yw) * step) >> 12);
		if (monster_fits(mi, nx, ny, &fz)) { m->x = nx; m->y = ny; }
		else {                                              /* slide along whatever is in the way, and say so */
			m->blocked = 1;
			if (monster_fits(mi, nx, m->y, &fz)) m->x = nx;
			else if (monster_fits(mi, m->x, ny, &fz)) m->y = ny;
		}
	}
	m->moving = (m->x != ox || m->y != oy);
	feet = m->z - m->eye;
	if (!world_fits(m->x, m->y, m->radius, feet, 1 << 20, &fz, &cz)) { const Cell *c = world_cell(m->x >> 8, m->y >> 8); fz = c->floor * FX; cz = c->ceil * FX; }
	if (wet) {                                              /* swims along its pitch, which points at its enemy */
		int ex, ey, ez, ea, d, sw = mt->speed * FX * SPEED_PCT / 12000;
		enemy_pos(m, &ex, &ey, &ez, &ea);
		d = vlen2((ex - m->x), (ey - m->y)) + 1;
		if (m->move && iabs(ez - m->z) > 64) m->z += (ez > m->z ? sw : -sw) * iabs(ez - m->z) / (iabs(ez - m->z) + d);
		if (m->jumpnext) m->vz = 1280;
		m->jumpnext = 0;
		m->z += m->vz >> 4; m->vz = m->vz * 23 / 24; m->z -= 11;
		m->onfloor = 0;
		if (m->z - m->eye <= fz) { m->z = fz + m->eye; if (m->vz < 0) m->vz = 0; m->onfloor = 1; }
		if (m->z + mabove(m) > cz) m->z = cz - mabove(m);
		return;
	}
	if (m->onfloor && m->jumpnext) { m->vz = 580; m->onfloor = 0; }
	m->jumpnext = 0;
	m->onfloor = (m->vz <= 0 && feet <= fz + 4);
	if (m->onfloor) { m->z = fz + m->eye; m->vz = 0; }
	else {
		m->z += m->vz >> 4; m->vz -= 28;
		if (m->z - m->eye < fz) { m->z = fz + m->eye; m->vz = 0; m->onfloor = 1; }
	}
	if (m->z + mabove(m) > cz && m->vz > 0) m->vz = 0;
}

/* monster.cpp monsteraction(): the thinking routine, every tick for every live monster */
static void monster_action(int mi) {
	Monster *m = &mons[mi]; const MT *mt = &mtypes[m->type];
	int ex, ey, ez, ealive, dx, dy, dz, dist, du, enemyyaw, d;
	if (m->enemy >= 0 && !mons[m->enemy].alive) { m->enemy = -1; m->anger = 0; }
	/* turn towards the target yaw at Cube's 0.5 degrees per ms */
	d = ((m->targetyaw - m->yaw + 2048) & 4095) - 2048;
	if (d > 95) d = 95; if (d < -95) d = -95;
	m->yaw = (m->yaw + d) & 4095;

	enemy_pos(m, &ex, &ey, &ez, &ealive);
	dx = ex - m->x; dy = ey - m->y; dz = ez - m->z;
	dist = vlen3(dx, dy, dz); du = dist / FX;

	if (m->blocked) {                                       /* ran into scenery */
		m->blocked = 0;
		if (!rnd(20000 / mt->speed)) m->jumpnext = 1;      /* try to jump over it (rare) */
		else if (m->trigger < now_ms && (m->mstate != M_HOME || !rnd(5))) {
			m->targetyaw = (m->targetyaw + 2048 + rnd(2048)) & 4095;     /* "random walk" pathfinding: turn 180-360 degrees */
			transition(m, M_SEARCH, 1, 400, 1000);
		}
	}
	enemyyaw = yaw_of(dx, dy);

	switch (m->mstate) {
	case M_PAIN: case M_ATTACKING: case M_SEARCH:
		if (m->trigger < now_ms) transition(m, M_HOME, 1, 100, 200);
		break;

	case M_SLEEP: {                                         /* wait for visual contact */
		int a = ((enemyyaw - m->yaw + 2048) & 4095) - 2048;  /* degrees off its facing */
		a = iabs(a) * 360 / 4096;
		if ((game_ticks + mi) & 7) return;                  /* look every 8th tick (Cube: every frame) */
		if (!ealive) return;
		/* the better the angle to the player, the further the monster can see / hear: Cube's rule with the ranges doubled
		   (Cube's 8 units behind it is less than a shotgun's reach) ... */
		if (du > sight_cells()) { if (m->aware > 0) m->aware--; return; }          /* past where you can see it, too */
		if (du >= 32 && (((game_ticks >> 3) + mi) & 3)) return;    /* the far ones look a quarter as often (a sight line is dear) */
		if (!world_los(m->x, m->y, m->z, ex, ey, ez)) { if (m->aware > 0) m->aware -= du >= 32 ? 4 : 1; if (m->aware < 0) m->aware = 0; return; }
		if (!(du < 16 || (du < 32 && a < 135) || (du < 64 && a < 90) || a < 30)) {
			/* ... and anything in plain sight notices you after a few steps: half a second facing you, two seconds
			   with its back to you (counted every 8 ticks) */
			m->aware += (a < 45 ? 4 : a < 90 ? 3 : a < 135 ? 2 : 1) * (du >= 32 ? 4 : 1);
			if (m->aware < 15) return;
		}
		m->aware = 0;
		transition(m, M_HOME, 1, 500, 200);
		snd_play_at(S_GRUNT1 + rnd(2), m->x, m->y, m->z);
		return;                                             /* sleeping monsters don't run physics */
	}

	case M_AIMING:                                          /* the delay between wanting to shoot and firing */
		if (m->trigger < now_ms) { monster_shoot(mi); transition(m, M_ATTACKING, 0, 600, 0); }
		break;

	case M_HOME:                                            /* has seen the enemy: heads straight for it, may fire any time */
		m->targetyaw = enemyyaw;
		if (m->trigger < now_ms) {
			if (!world_los(m->x, m->y, m->z, ex, ey, ez)) transition(m, M_HOME, 1, 800, 500);   /* get close, then search */
			else if (!rnd(du / 3 + 1) && ealive) {          /* the closer, the more likely it fires */
				m->atx = ex; m->aty = ey; m->atz = ez;
				transition(m, M_AIMING, 0, mt->lag, 10);
			} else transition(m, M_HOME, 1, mt->rate, 0);
		}
		break;
	}
	monster_physics(mi);
}

static void update_monsters(void) {
	int i;
	for (i = 0; i < nmons; i++) {
		Monster *m = &mons[i];
		if (m->bot) continue;                              /* bots think in bots.inc */
		if (m->painflash > 0) m->painflash--;
		if (m->alive) monster_action(i);
	}
}

static int touch(int x, int y, int rad) { int dx = pl.x - x, dy = pl.y - y; return iabs(dx) < rad && iabs(dy) < rad && dx * dx + dy * dy < rad * rad; }

static void update_items(void) {
	int i;
	for (i = 0; i < nitems; i++) {
		Item *it = &items[i]; int ok = 0;
		if (it->taken || !touch(it->x, it->y, 2 * FX) || iabs(pl.z - EYE - it->z) > 3 * FX) continue;
		switch (it->type) {
		case E_SHELLS:  if (pl.ammo[GUN_SG] < 50)  { pl.ammo[GUN_SG] += 10; if (pl.ammo[GUN_SG] > 50) pl.ammo[GUN_SG] = 50; ok = 1; } break;
		case E_BULLETS: if (pl.ammo[GUN_CG] < 100) { pl.ammo[GUN_CG] += 20; if (pl.ammo[GUN_CG] > 100) pl.ammo[GUN_CG] = 100; ok = 1; } break;
		case E_ROCKETS: if (pl.ammo[GUN_RL] < 25)  { pl.ammo[GUN_RL] += 5; if (pl.ammo[GUN_RL] > 25) pl.ammo[GUN_RL] = 25; ok = 1; } break;
		case E_ROUNDS:  if (pl.ammo[GUN_RIFLE] < 25) { pl.ammo[GUN_RIFLE] += 5; if (pl.ammo[GUN_RIFLE] > 25) pl.ammo[GUN_RIFLE] = 25; ok = 1; } break;
		case E_HEALTH:  if (pl.health < 100) { pl.health += 25; if (pl.health > 100) pl.health = 100; ok = 1; } break;
		case E_BOOST:   if (pl.health < 200) { pl.health += 50; if (pl.health > 200) pl.health = 200; ok = 1; } break;
		case E_GREENARMOUR:  if (pl.armour < 100 || pl.armourtype < 1) { pl.armour = 100; pl.armourtype = 1; ok = 1; } break;
		case E_YELLOWARMOUR: if (pl.armour < 150 || pl.armourtype < 2) { pl.armour = 150; pl.armourtype = 2; ok = 1; } break;
		case E_CLIPS:   if (pl.ammo[GUN_PISTOL] < 60) { pl.ammo[GUN_PISTOL] += 15; if (pl.ammo[GUN_PISTOL] > 60) pl.ammo[GUN_PISTOL] = 60; ok = 1; } break;
		}
		if (ok) {
			it->taken = 1; it->respawn = game_mode != GM_SP ? game_ticks + 20 * 60 : 0;
			snd_play(it->type >= E_GREENARMOUR ? S_ITEMARMOUR : it->type >= E_HEALTH ? S_ITEMHEALTH : S_ITEMAMMO);
			if (it->type == E_ROCKETS && pl.ammo[GUN_RL] == 5) pl.gun = GUN_RL;
			if (it->type == E_ROUNDS && pl.ammo[GUN_RIFLE] == 5) pl.gun = GUN_RIFLE;
			if (it->type == E_BULLETS && pl.ammo[GUN_CG] == 20) pl.gun = GUN_CG;
			if (it->type == E_SHELLS && pl.ammo[GUN_SG] == 10) pl.gun = GUN_SG;
			if (it->type == E_CLIPS && pl.ammo[GUN_PISTOL] == 15 && pl.gun == GUN_FIST) pl.gun = GUN_PISTOL;
		}
	}
}

/* Cube's entinmap: nudge a body to the nearest place it fits, at ever larger random offsets, and drop it to the floor */
static void entinmap(int *x, int *y, int *z, int eye, int rad) {
	int i, fz, cz;
	for (i = 0; i < 100; i++) {
		int dx = (rnd(21) - 10) * i * FX / 10, dy = (rnd(21) - 10) * i * FX / 10;
		if (world_fits(*x + dx, *y + dy, rad, -128 * FX, 256 * FX, &fz, &cz)) { *x += dx; *y += dy; *z = fz + eye; return; }
	}
}

/* Cube's teleport: to the TELEDEST whose attr2 is this teleport's tag; also used by monsters */
static int teleport(const Trig *t, int *x, int *y, int *z, int *yaw, int eye, int rad) {
	int i;
	for (i = 0; i < world_nents(); i++) {
		const MapEnt *d = world_ent(i);
		if (d->type != E_TELEDEST || d->attr2 != t->attr1) continue;
		*x = d->x * FX; *y = d->y * FX; *z = d->z * FX;
		*yaw = (d->attr1 * 4096 / 360) & 4095;
		entinmap(x, y, z, eye, rad);
		return 1;
	}
	return 0;
}

/* Cube's endsp: "you reached the exit!" / "you have cleared the map!" and the intermission */
static void endsp(int allkilled) {
	snd_say(allkilled ? V_CLEANSWEEP : V_WHOSNEXT, 1); quip_at = 0;
	if (level_done) return;
	level_done = allkilled ? 2 : 1;
	level_secs = (game_ticks - level_t0) / 60;
	sprintf(level_msg, "\n\n\n\n\n\n   %s\n\n   SCORE: %d KILLS IN %d SECONDS\n\n   PRESS START",
	        allkilled ? "YOU HAVE CLEARED THE MAP!" : "YOU REACHED THE EXIT!", pl.kills, level_secs);
	game_msg = level_msg; game_msg_timer = 1 << 30;
}

/* Cube's trigger(): open the tagged area, run the level script (only `map <next>` is supported: attr4 bit 0), type 2 = exit */
static void trigger(const Trig *t) {
	if (!t->attr1) return;
	world_settag(t->attr1, t->attr2);
	if (t->attr2 != 3) snd_play(S_RUMBLE);
	tag_open[t->attr1 >> 3] |= 1 << (t->attr1 & 7);
	if ((t->attr4 & 1) || t->attr2 == 2) endsp(0);
}

/* Cube's checkitems/pickup for carrots, teleports and jump pads: within 2.5 units (4 for a teleport) of the point one eye
 * height above the floor under the entity */
static int lastteleport = -1000, lastjumppad = -1000;
static void update_trigs(void) {
	int i;
	for (i = 0; i < ntrigs; i++) {
		Trig *t = &trigs[i]; int r = t->type == E_TELEPORT ? 4 * FX : 640, dx = pl.x - t->x, dy = pl.y - t->y, dz = pl.z - (t->z + EYE);
		if (t->type == E_CARROT && !t->spawned) continue;
		if (iabs(dx) >= r || iabs(dy) >= r || iabs(dz) >= r || dx * dx + dy * dy + dz * dz >= r * r) continue;
		if (t->type == E_CARROT) { t->spawned = 0; trigger(t); }
		else if (t->type == E_TELEPORT) {
			if (game_ticks - lastteleport < 30) continue;          /* 500 ms */
			lastteleport = game_ticks;
			if (teleport(t, &pl.x, &pl.y, &pl.z, &pl.yaw, EYE, PRAD)) { pl.pitch = 0; pl.vx = pl.vy = pl.vz = 0; snd_play(S_TELEPORT); }
		} else {
			if (game_ticks - lastjumppad < 18) continue;           /* 300 ms */
			lastjumppad = game_ticks;
			/* Cube adds (attr3, attr2, attr1) / 10 to the velocity, where a jump is 1.7 up and 1.0 is running speed (22 u/s) */
			pl.vz = t->attr1 * JUMPVZ / 17; snd_play(S_JUMPPAD);
			pl.vx += (int8_t)t->attr3 * 94 * 16 / 10; pl.vy += (int8_t)t->attr2 * 94 * 16 / 10;
			pl.onfloor = 0;
		}
	}
	/* monsters use teleports too (monster.cpp monsterthink) */
	for (i = 0; i < ntrigs; i++) {
		const Trig *t = &trigs[i]; int j;
		if (t->type != E_TELEPORT) continue;
		for (j = 0; j < nmons; j++) {
			Monster *m = &mons[j]; int dx = m->x - t->x, dy = m->y - t->y, dz = m->z - (t->z + m->eye), r = 4 * FX;
			if (!m->alive || m->bot || iabs(dx) >= r || iabs(dy) >= r || iabs(dz) >= r || dx * dx + dy * dy + dz * dz >= r * r) continue;
			teleport(t, &m->x, &m->y, &m->z, &m->yaw, m->eye, m->radius);
		}
	}
}

#include "bots.inc"

/* ------------- attract-mode demo: walk the route in demo_path.h, look where it goes, shoot what it meets ------------- */
#include "demo_path.h"
static int demo_seg, demo_along, demo_yaw;
void demo_begin(void) {
	game_start(DEMO_MAP);
	world_settag(0, 0);                     /* a player would have opened the doors: the demo walks through them */
	demo_mode = 1; demo_done = 0; demo_seg = 0; demo_along = 0;
	pl.x = demo_pts[0][0] * 16; pl.y = demo_pts[0][1] * 16;
	pl.yaw = demo_yaw = yaw_of(demo_pts[1][0] - demo_pts[0][0], demo_pts[1][1] - demo_pts[0][1]);
	pl.ammo[GUN_SG] = pl.ammo[GUN_CG] = 999; pl.gun = GUN_CG;
	game_msg_timer = 0;
}

static int turn_toward(int from, int to, int rate) {
	int d = ((to - from + 2048) & 4095) - 2048;
	if (d > rate) d = rate;
	if (d < -rate) d = -rate;
	return (from + d) & 4095;
}

static void demo_tick(void) {
	int step = 55, fz, cz, i, tx, ty, want, best = 14 * FX, target = -1;
	/* walk: advance `step` along the polyline, then place the player there */
	while (step > 0 && demo_seg < DEMO_NPTS - 1) {
		int ax = demo_pts[demo_seg][0] * 16, ay = demo_pts[demo_seg][1] * 16, bx = demo_pts[demo_seg + 1][0] * 16, by = demo_pts[demo_seg + 1][1] * 16;
		int left = vlen2((bx - ax), (by - ay)) - demo_along;
		if (step < left) { demo_along += step; step = 0; }
		else { step -= left; demo_seg++; demo_along = 0; }
	}
	if (demo_seg >= DEMO_NPTS - 1) { demo_done = 1; return; }
	{
		int ax = demo_pts[demo_seg][0] * 16, ay = demo_pts[demo_seg][1] * 16, bx = demo_pts[demo_seg + 1][0] * 16, by = demo_pts[demo_seg + 1][1] * 16;
		int len = vlen2((bx - ax), (by - ay));
		if (len > 0) { pl.x = ax + (bx - ax) * demo_along / len; pl.y = ay + (by - ay) * demo_along / len; }
	}
	if (world_fits(pl.x, pl.y, 64, -128 * FX, 256 * FX, &fz, &cz)) pl.z += (fz + EYE - pl.z) / 3;
	/* look a little ahead along the route */
	tx = demo_pts[demo_seg + 1][0] * 16 - pl.x; ty = demo_pts[demo_seg + 1][1] * 16 - pl.y;
	if (iabs(tx) + iabs(ty) < 3 * FX && demo_seg + 2 < DEMO_NPTS) { tx = demo_pts[demo_seg + 2][0] * 16 - pl.x; ty = demo_pts[demo_seg + 2][1] * 16 - pl.y; }
	want = yaw_of(tx, ty);
	/* ...unless there is a live monster in sight, roughly ahead: then turn to it and shoot */
	for (i = 0; i < nmons; i++) {
		Monster *m = &mons[i]; int dx = m->x - pl.x, dy = m->y - pl.y, d, rel;
		if (!m->alive || iabs(dx) > best || iabs(dy) > best) continue;
		d = vlen2(dx, dy);
		rel = ((yaw_of(dx, dy) - want + 2048) & 4095) - 2048;
		if (d >= best || iabs(rel) > 800 || !world_los(pl.x, pl.y, pl.z, m->x, m->y, m->z)) continue;
		best = d; target = i;
	}
	if (target >= 0) want = yaw_of(mons[target].x - pl.x, mons[target].y - pl.y);
	pl.yaw = demo_yaw = turn_toward(demo_yaw, want, target >= 0 ? 70 : 34);
	pl.pitch = 0;
	if (target >= 0 && iabs(((pl.yaw - want + 2048) & 4095) - 2048) < 120) {
		pl.gun = best < 6 * FX ? GUN_SG : GUN_CG;
		pl.ammo[GUN_SG] = pl.ammo[GUN_CG] = 999;
		fire_player();
	}
}

/* ------------- save games: the whole game state, as plain data ------------- */
typedef struct {
	char magic[4]; int version, map, diff, elapsed, total;
	Player pl;
	int nmons; uint8_t mons[MAXMON][offsetof(Monster, bot)];     /* (a single-player save has no bots: their fields stay out) */
	int nitems; uint8_t taken[MAXITEM];
	int ntrigs; uint8_t spawned[MAXTRIG];
	uint8_t tags[32]; unsigned rng;
	uint8_t seen[2048];                  /* version 4 */
} SaveState;

int game_save_size(void) { return sizeof(SaveState); }
int game_current_map(void) { return curmap; }
int game_elapsed_secs(void) { return (game_ticks - level_t0) / 60; }

void game_save(void *buf) {
	SaveState *s = (SaveState *)buf; int i;
	memset(s, 0, sizeof(*s));
	memcpy(s->magic, "CPS1", 4); s->version = 5; s->map = curmap; s->diff = difficulty;
	s->elapsed = game_ticks - level_t0; s->total = total_monsters;
	s->pl = pl;
	s->nmons = nmons; for (i = 0; i < nmons; i++) memcpy(s->mons[i], &mons[i], sizeof(s->mons[i]));
	s->nitems = nitems; for (i = 0; i < nitems; i++) s->taken[i] = items[i].taken;
	s->ntrigs = ntrigs; for (i = 0; i < ntrigs; i++) s->spawned[i] = trigs[i].spawned;
	memcpy(s->tags, tag_open, 32); s->rng = rngs;
	memcpy(s->seen, world_seen, sizeof(world_seen));
}

int game_load(const void *buf) {
	const SaveState *s = (const SaveState *)buf; int i;
	if (!memcmp(s->magic, "CPS1", 4) && (s->version == 3 || s->version == 4)) {
		/* saved before the pistol: the player's ammo had no pistol slot - open one (empty) and move the rest along.
		   (the caller's buffer has room: the card file is bigger than a save) */
		int cut = offsetof(SaveState, pl) + offsetof(Player, ammo) + (NUMGUNS - 1) * sizeof(int);
		memmove((uint8_t *)buf + cut + sizeof(int), (const uint8_t *)buf + cut, sizeof(SaveState) - cut - sizeof(int));
		memset((uint8_t *)buf + cut, 0, sizeof(int));
		((SaveState *)buf)->version |= 0x100;             /* converted: never twice */
	}
	if (memcmp(s->magic, "CPS1", 4) || (s->version & 0xff) < 3 || (s->version & 0xff) > 5 || s->map < 0 || s->map >= NUM_MAPS) return 0;
	difficulty = s->diff;
	game_start(s->map);
	if (s->nmons != nmons || s->nitems != nitems || s->ntrigs != ntrigs) return 0;      /* not this map's save */
	pl = s->pl;
	for (i = 0; i < nmons; i++) { memset(&mons[i], 0, sizeof(Monster)); memcpy(&mons[i], s->mons[i], sizeof(s->mons[i])); }
	for (i = 0; i < nitems; i++) items[i].taken = s->taken[i];
	for (i = 0; i < ntrigs; i++) trigs[i].spawned = s->spawned[i];
	memcpy(tag_open, s->tags, 32);
	for (i = 1; i < 256; i++) if (tag_open[i >> 3] & (1 << (i & 7))) world_settag(i, 0);
	level_t0 = game_ticks - s->elapsed; rngs = s->rng;
	if ((s->version & 0xff) >= 4) { memcpy(world_seen, s->seen, sizeof(world_seen)); seen_gen++; }
	memset(projs, 0, sizeof(projs));
	game_msg_timer = 0;
	return 1;
}

static const int8_t gun_order[6] = { GUN_FIST, GUN_PISTOL, GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE };     /* Triangle / Square cycle */
static int next_gun(int dir) {
	int k, i;
	for (k = 0; k < 6 && gun_order[k] != pl.gun; k++) ;
	for (i = 0; i < 6; i++) {
		k = (k + dir + 6) % 6;
		if (pl.ammo[gun_order[k]] > 0) return gun_order[k];
	}
	return pl.gun;
}

static int dpad_hold, air_ticks;
int pl_flying;                               /* quickplay warm-up: no-clip, you fly through everything */
static void move_player(int held, int pressed, int lx, int ly, int rx, int ry) {
	int fwd = -ly, side = lx;          /* stick: up = negative y */
	int nx, ny, fz, cz, feet, mf, ms, turn, look;
	if (held & PAD_UP) fwd = 127;
	if (held & PAD_DOWN) fwd = -127;
	if (held & PAD_L1) side = -127;
	if (held & PAD_R1) side = 127;
	/* stick response curve: fine control near the centre, full speed at the edge; D-pad ramps up the longer it is held */
	turn = rx * iabs(rx) / 127; look = ry * iabs(ry) / 127;
	if (opt_invert) look = -look;
	if (held & (PAD_LEFT | PAD_RIGHT)) { if (dpad_hold < 40) dpad_hold++; turn = ((held & PAD_RIGHT) ? 1 : -1) * (12 + dpad_hold * 2); } else dpad_hold = 0;
	pl.yaw = (pl.yaw + (turn * 10 * TURN_PCT / 100) / 8 * -1 + 4096) & 4095;
	pl.pitch += look * (12 + opt_look * 12) / 80;     /* look speed 1 = the old PS1 tuning, 10 = 5.5x */ if (pl.pitch > 900) pl.pitch = 900; if (pl.pitch < -900) pl.pitch = -900;

	mf = fwd * (102 * SPEED_PCT / 100) / 127; ms = side * (102 * SPEED_PCT / 100) / 127;
	pl_flying = qp_waiting && game_mode != GM_SP;
	if (pl_flying) {                                    /* the warm-up's no-clip: where you look, through anything; X rises */
		int f2 = mf * 3 / 2, n = world_ssize() * FX;
		ms = ms * 3 / 2;
		mf = f2 * icos(pl.pitch) >> 12;
		pl.z -= f2 * isin(pl.pitch) >> 12;
		if (held & PAD_CROSS) pl.z += 90;
		pl.x += (-isin(pl.yaw) * mf + icos(pl.yaw) * ms) >> 12;
		pl.y += (icos(pl.yaw) * mf + isin(pl.yaw) * ms) >> 12;
		if (pl.x < 3 * FX) pl.x = 3 * FX;
		if (pl.y < 3 * FX) pl.y = 3 * FX;
		if (pl.x > n - 3 * FX) pl.x = n - 3 * FX;
		if (pl.y > n - 3 * FX) pl.y = n - 3 * FX;
		if (pl.z < -120 * FX) pl.z = -120 * FX;
		if (pl.z > 120 * FX) pl.z = 120 * FX;
		pl.vx = pl.vy = pl.vz = 0; pl.onfloor = 0; pl.inwater = 0; air_ticks = 0;
		return;
	}
	/* water (physics.cpp moveplayer): you are in it while it is above eye level - 0.5; half speed, and forward follows the
	   view up and down, so you swim where you look */
	{ int w = in_water(pl.z);                              /* splash going in and coming out */
	  if (w && !pl.inwater) { snd_play_at(S_SPLASH2, pl.x, pl.y, pl.z); pl.vz = 0; }
	  else if (!w && pl.inwater) { snd_play_at(S_SPLASH1, pl.x, pl.y, pl.z); pl.vz *= 2; air_ticks = 0; }   /* Cube keeps vel.z: out of the water it moves at full speed, twice the swim */
	  pl.inwater = w; }
	if (pl.inwater) {
		int swim = mf / 2;
		ms /= 2;
		mf = swim * icos(pl.pitch) >> 12;
		pl.z -= swim * isin(pl.pitch) >> 12;                 /* pitch > 0 looks down */
	}
	nx = pl.x + ((-isin(pl.yaw) * mf + icos(pl.yaw) * ms) >> 12);
	ny = pl.y + ((icos(pl.yaw) * mf + isin(pl.yaw) * ms) >> 12);
	nx += pl.vx >> 4; ny += pl.vy >> 4;              /* jump pad push (FX/tick x16), slowed by Cube's friction */
	pl.vx = pl.vx * (pl.onfloor ? 861 : 972) / 1000; pl.vy = pl.vy * (pl.onfloor ? 861 : 972) / 1000;
	feet = pl.z - EYE;
	if (world_fits(nx, pl.y, PRAD, feet, STEPMAX, &fz, &cz) && !player_blocked(nx, pl.y)) pl.x = nx;
	if (world_fits(pl.x, ny, PRAD, feet, STEPMAX, &fz, &cz) && !player_blocked(pl.x, ny)) pl.y = ny;
	if ((pl.x != nx || pl.y != ny) && (mf || ms) && !demo_mode && !link_on) {      /* walked into something: a door that is still shut? */
		int cx, cy;
		for (cy = (ny - PRAD - 64) >> 8; cy <= (ny + PRAD + 64) >> 8; cy++)
			for (cx = (nx - PRAD - 64) >> 8; cx <= (nx + PRAD + 64) >> 8; cx++) {
				const Cell *c = world_cell(cx, cy); int tg = c->tag;
				if (!tg || c->type != SOLID_T || (complained[tg >> 3] & (1 << (tg & 7)))) continue;
				if (game_ticks - last_complain < 1800) continue;          /* 30 s between complaints */
				static const uint8_t moan[3] = { V_LOCKED, V_SWITCH, V_COMEON };
				if (snd_say(moan[rnd(3)], 0)) { complained[tg >> 3] |= 1 << (tg & 7); last_complain = game_ticks; }
			}
	}
	/* the floor and ceiling under the whole body, whatever the step (swimming up beside a ledge can put part of you over it) */
	if (!world_fits(pl.x, pl.y, PRAD, feet, 1 << 20, &fz, &cz)) { const Cell *c = world_cell(pl.x >> 8, pl.y >> 8); fz = c->floor * FX; cz = c->ceil * FX; }

	feet = pl.z - EYE;
	if (pl.inwater) {
		/* float slowly down; a jump is a stroke upwards, which is how you climb out onto a ledge at the surface */
		/* Cube: a jump sets vel.z = 1.7, moved at speed/2000 per ms in water and damped by friction 20 (1/24 a frame):
		   0.31 units a tick at first, about 7 units in all - enough to burst out of the water onto a ledge */
		if (pressed & PAD_CROSS) { pl.vz = 1280; snd_play(S_JUMP); }
		pl.z += pl.vz >> 4; pl.vz = pl.vz * 23 / 24;
		pl.z -= 11;                                          /* Cube's in-water drop: 2.5 units a second */
		pl.onfloor = 0;
		if (pl.z - EYE <= fz) { pl.z = fz + EYE; if (pl.vz < 0) pl.vz = 0; pl.onfloor = 1; }
	} else {
		/* Cube's air physics (physics.cpp moveplayer): a jump sets vel.z = 1.7, i.e. 1.7 x running speed upwards, which
		   air friction (30) eats at 1/36 a frame; gravity is a separate drop of (19 + ms in the air / 15) / 2000 units a
		   millisecond. That gives Cube's 4.3 unit jump, 0.7 s in the air; on top of it the stair rise mounts a ledge */
		int drop = 40 + air_ticks * 237 / 100;
		pl.onfloor = (feet <= fz + 4 && (pl.vz >> 4) <= drop);
		if (pl.onfloor) {
			if (air_ticks > 48) snd_play(S_LAND);             /* Cube: a thud after more than 800 ms in the air */
			air_ticks = 0;
			pl.z = fz + EYE; pl.vz = 0;
			if (pressed & PAD_CROSS) { pl.vz = JUMPVZ; pl.onfloor = 0; snd_play(S_JUMP); }
		} else air_ticks++;
		if (!pl.onfloor) {
			pl.vz -= pl.vz / 36; pl.z += (pl.vz >> 4) - drop;
			if (pl.z - EYE < fz) { pl.z = fz + EYE; pl.vz = 0; pl.onfloor = 1; }
		}
	}
	if (pl.z + ABOVE > cz) { pl.z = cz - ABOVE; if (pl.vz > 0) pl.vz = 0; }
}

#include "linkplay.inc"

/* one human's part of a tick, with that human in pl */
static void human_timers(void) {
	pl_speed = iabs(pl.x - pl_lx0) + iabs(pl.y - pl_ly0); if (pl_speed > 400) pl_speed = 0; pl_lx0 = pl.x; pl_ly0 = pl.y;   /* (a teleport is not speed) */
	if (pl.hurt > 0) pl.hurt--;
	if (pl.fireflash > 0) pl.fireflash--;
	if (pl.gunwait > 0) pl.gunwait--;
}
static void human_act(int held, int pressed, int lx, int ly, int rx, int ry) {
	if (pl.dead) {
		if (game_mode == GM_SP && (pressed & PAD_START)) { int dd = pl.deaths; game_start(curmap); pl.deaths = dd; }
		return;
	}
	if ((pressed & PAD_SELECT) && (!link_on || link_phase == link_me)) automap_on = !automap_on;
	move_player(held, pressed, lx, ly, rx, ry);
	{   /* the automap knows the ground around you, even with the map up */
		int cx = pl.x >> 8, cy = pl.y >> 8, x, y;
		for (y = cy - 4; y <= cy + 4; y += 2) for (x = cx - 4; x <= cx + 4; x += 2)
			if ((unsigned)x < 256 && (unsigned)y < 256) SEEN_MARK(x, y);
	}
	{ int og = pl.gun;
	  if (pressed & PAD_TRIANGLE) pl.gun = next_gun(1);
	  if (pressed & PAD_SQUARE) pl.gun = next_gun(-1);
	  if (pl.gun != og) snd_play(S_WEAPLOAD); }
	if (qp_waiting && game_mode != GM_SP) {              /* quickplay warm-up: fire starts the match; nothing to pick up yet */
		if (pressed & (PAD_R2 | PAD_L2)) { if (link_on) link_rdy[link_phase] = 1; else game_quick_begin(); }   /* (no teleporters: you are flying through them) */
	} else {
		if (held & (PAD_R2 | PAD_L2)) fire_player();
		if (pl.ammo[pl.gun] <= 0 && pl.gun != GUN_FIST) pl.gun = next_gun(-1);
		update_items(); update_trigs();
	}
}
static void common_timers(void) {
	int i;
	game_ticks++;
	if (game_msg_timer > 0) game_msg_timer--;
	for (i = 0; i < MAXFX; i++) if (fxs[i].life > 0) fxs[i].life--;
}
static void human_quip(void) { if (quip_at && game_ticks >= quip_at) { if (!pl.dead) snd_say(quip_id, 0); quip_at = 0; } }

void game_tick(int held, int pressed, int lx, int ly, int rx, int ry) {
	common_timers();
	human_timers();
	if (demo_mode) {
		demo_tick();
		pl.hurt = 0;
		update_monsters();
		update_projs();
		return;
	}
	human_quip();
	if (level_done) return;                                   /* intermission: the world stands still; START is the menu's */
	human_act(held, pressed, lx, ly, rx, ry);
	if (game_mode != GM_SP) { quick_human(pressed); quick_world(); }
	update_monsters();
	update_projs();
	if (game_mode == GM_SP && total_monsters && pl.kills == total_monsters) endsp(1);
}

/* a link game's tick: human 0, human 1 (swapped into pl), then the world, with human 0 back in pl */
void game_tick_link(const LinkPad *p0, const LinkPad *p1) {
	const LinkPad *in[2]; int h;
	in[0] = p0; in[1] = p1;
	if (!link_on) return;
	common_timers();
	for (h = 0; h < 2; h++) {
		const LinkPad *p = in[h];
		if (h) link_swap();
		human_timers();
		human_quip();
		if (!level_done) {
			link_apply_pend();
			human_act(p->held, p->pressed, p->lx, p->ly, p->rx, p->ry);
			quick_human(p->pressed);
		}
		if (h) link_swap();
	}
	if (level_done) return;
	for (h = 0; h < 2; h++) if (in[h]->quit) {             /* a player left: the other wins by default */
		sprintf(qp_msg, "PLAYER %d LEFT - PLAYER %d WINS", h + 1, 2 - h);
		match_end(qp_msg);
		return;
	}
	if (qp_waiting && link_rdy[0] && link_rdy[1]) {         /* both ready: everyone in, at once */
		quick_begin();
		link_swap(); player_spawn_quick(); link_swap();
	}
	quick_world();
	update_monsters();
	update_projs();
}
