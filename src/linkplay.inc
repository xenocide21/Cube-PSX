/* linkplay.inc - two humans, one per console, on the link cable.  Included by game.c just before game_tick.
 *
 * Both consoles run exactly the same game from the same pads, tick by tick (link.c carries the pads).  Human 0 (the
 * host's) lives in `pl`, as in a one-console game; human 1 is kept in hum_other and shown to the rest of the game by
 * the proxy: a bot (bot == 2) that does not think, standing where human 1 stands.  Bots aim at it, shots hit it, it
 * carries flags and holds the hill.  Damage it takes is queued for its human.
 *
 * link_swap() exchanges the two: human 1 into `pl`, human 0 into the store and the proxy, and every "-1 = the player"
 * reference relabelled.  Each tick runs human 0's part, swaps, runs human 1's part, swaps back, then the world.  The
 * guest console also swaps to draw its own view.  Swapping twice changes nothing the simulation reads. */

typedef struct {
	Player p;
	int dpad_hold, air_ticks, speed, lx0, ly0, team, frags, caps, respawn_at, lastteleport, lastjumppad;
	int quip_at, quip_id, quip_ready, last_quip, last_grunt;
	int look, invert, aim;
	int m_anim, m_animt0, m_deadvar, m_alive;                /* the proxy's look while it stands for this human (display only) */
} Hum;
static Hum hum_other;                                      /* the human not in pl */
static int link_rdy[2];                                    /* warm-up: who has pressed fire */
#define NPEND 8
static struct { int16_t dmg, src; } pend[2][NPEND];        /* damage queued for a human (by human number); src encoded */
static int npend[2];
static int link_saved_move = -1, link_saved_turn, link_saved_look, link_saved_invert, link_saved_aim, link_saved_diff;

static int src_dmg(int src, int d) { return src >= 0 && src != link_P ? mon_dmg(d) : d; }
static int hum_of(int who) { return who == -1 ? link_phase : (link_on && who == link_P) ? 1 - link_phase : -1; }
static int my_team(void) { return link_on && link_phase != link_me ? hum_other.team : pl_team; }
static int link_other_speed(void) { return hum_other.speed; }
int game_link_ready(int h) { return link_rdy[h & 1]; }
int game_link_frags(int h) { return h == link_phase ? pl_frags : link_P >= 0 ? mons[link_P].frags : 0; }
const Player *game_listener(void) { return link_on && link_phase != link_me ? &hum_other.p : &pl; }
int game_sound_remote(void) { return link_on && link_phase != link_me; }

static void hum_get(Hum *h) {
	h->p = pl; h->dpad_hold = dpad_hold; h->air_ticks = air_ticks; h->speed = pl_speed; h->lx0 = pl_lx0; h->ly0 = pl_ly0;
	h->team = pl_team; h->frags = pl_frags; h->caps = pl_caps; h->respawn_at = pl_respawn_at; h->lastteleport = lastteleport; h->lastjumppad = lastjumppad;
	h->quip_at = quip_at; h->quip_id = quip_id; h->quip_ready = quip_ready; h->last_quip = last_quip; h->last_grunt = last_grunt;
	h->look = opt_look; h->invert = opt_invert; h->aim = opt_aim;
}
static void hum_put(const Hum *h) {
	pl = h->p; dpad_hold = h->dpad_hold; air_ticks = h->air_ticks; pl_speed = h->speed; pl_lx0 = h->lx0; pl_ly0 = h->ly0;
	pl_team = h->team; pl_frags = h->frags; pl_caps = h->caps; pl_respawn_at = h->respawn_at; lastteleport = h->lastteleport; lastjumppad = h->lastjumppad;
	quip_at = h->quip_at; quip_id = h->quip_id; quip_ready = h->quip_ready; last_quip = h->last_quip; last_grunt = h->last_grunt;
	opt_look = h->look; opt_invert = h->invert; opt_aim = h->aim;
}

/* the proxy takes on the human in hum_other */
static void proxy_set(void) {
	Monster *m = &mons[link_P]; Hum *h = &hum_other; int alive = !h->p.dead;
	m->anim = h->m_anim; m->animt0 = h->m_animt0; m->deadvar = h->m_deadvar;
	if (h->m_alive && !alive) { m->anim = ANIM_DEATH; m->animt0 = game_ticks; m->deadvar = (h->p.x + h->p.y + game_ticks) / 7 % 3; }
	else if (alive && !h->m_alive) { m->anim = ANIM_IDLE; m->animt0 = game_ticks; }
	if (alive && h->p.fireflash >= 3 && m->anim != ANIM_ATTACK) { m->anim = ANIM_ATTACK; m->animt0 = game_ticks; }
	m->alive = h->m_alive = alive;
	m->x = h->p.x; m->y = h->p.y; m->z = h->p.z; m->yaw = m->targetyaw = m->myaw = h->p.yaw;
	m->health = h->p.health; m->gun = h->p.gun; m->team = h->team; m->frags = h->frags; m->caps = h->caps;
	m->moving = alive && h->speed > 20; m->painflash = h->p.hurt > 12 ? 1 : 0;
	m->enemy = -2;
}
static void proxy_keep(void) {                              /* what the proxy changed, back into its human */
	Monster *m = &mons[link_P]; Hum *h = &hum_other;
	h->frags = m->frags; h->caps = m->caps; h->m_anim = m->anim; h->m_animt0 = m->animt0; h->m_deadvar = m->deadvar;
}

static int relab(int v) { return v == -1 ? link_P : v == link_P ? -1 : v; }
static void link_swap(void) {
	Hum cur; int i;
	proxy_keep();
	hum_get(&cur); hum_put(&hum_other); hum_other = cur;
	link_phase ^= 1;
	proxy_set();
	for (i = 0; i < nmons; i++) mons[i].enemy = relab(mons[i].enemy);
	for (i = 0; i < MAXPROJ; i++) if (projs[i].life) projs[i].owner = relab(projs[i].owner);
	for (i = 0; i < nflags; i++) if (flags[i].state == 1) flags[i].carrier = relab(flags[i].carrier);
	mons[link_P].enemy = -2;
}

/* damage for the human the proxy stands for: kept by human number, with who did it written so it survives a swap
   (-10 - h = human h) */
static int enc(int who) { int h = hum_of(who); return h >= 0 ? -10 - h : who; }
static int dec(int v) { return v <= -10 ? (-10 - v == link_phase ? -1 : link_P) : v; }
static void link_pend(int dmg, int attacker) {
	int h = 1 - link_phase;
	if (npend[h] >= NPEND || dmg <= 0) return;
	pend[h][npend[h]].dmg = src_dmg(attacker, dmg); pend[h][npend[h]].src = enc(attacker); npend[h]++;
}
static void link_apply_pend(void) {
	int h = link_phase, i;
	for (i = 0; i < npend[h]; i++) { hurt_src = dec(pend[h][i].src); hurt_player(pend[h][i].dmg); }
	npend[h] = 0;
}

void game_link_view(int on) {
	if (!link_on || link_me == 0) return;
	if ((on && link_phase == 0) || (!on && link_phase == 1)) link_swap();
}

void game_start_link(const LinkSetup *s, int me) {
	Monster *m; const MT *mt = &mtypes[0];
	if (link_saved_move < 0) {
		link_saved_move = opt_move; link_saved_turn = opt_turn; link_saved_look = opt_look; link_saved_invert = opt_invert;
		link_saved_aim = opt_aim; link_saved_diff = difficulty;
	}
	/* everything either console might remember from before starts the same */
	link_on = 0; link_P = -1000; link_phase = 0; link_me = me;
	memset(&pl, 0, sizeof(pl)); memset(&hum_other, 0, sizeof(hum_other)); memset(projs, 0, sizeof(projs));
	memset(npend, 0, sizeof(npend)); link_rdy[0] = link_rdy[1] = 0;
	dpad_hold = air_ticks = pl_speed = pl_lx0 = pl_ly0 = 0; lastteleport = lastjumppad = -1000;
	last_quip = -1; last_grunt = -100; last_complain = -100000; pl_respawn_at = 0;
	game_ticks = 0; rngs = s->seed ? s->seed : 12345;
	difficulty = s->skill > 2 ? 1 : s->skill; opt_move = s->move; opt_turn = s->turn;
	opt_look = s->look[0]; opt_invert = s->invert[0]; opt_aim = s->aim[0];
	game_start_quick(s->map, s->mode, s->bots);             /* human 0 is pl, red */
	link_on = 1;
	/* human 1, and the proxy that stands for it */
	link_P = nmons++;
	m = &mons[link_P]; memset(m, 0, sizeof(*m));
	m->bot = 2; m->type = 0; m->scale = mt->bscale; m->radius = PRAD; m->eye = EYE; m->enemy = -2;
	hum_get(&hum_other);                                     /* a copy of a new human 0, then made human 1 */
	hum_other.team = s->mode == GM_DM ? 1 : s->teams == LT_TOGETHER ? 0 : 1;
	hum_other.frags = hum_other.caps = 0; hum_other.look = s->look[1]; hum_other.invert = s->invert[1]; hum_other.aim = s->aim[1];
	hum_other.m_alive = 0; hum_other.m_animt0 = -100000;
	qp_teams(s->mode == GM_DM ? -1 : hum_other.team);
	link_swap(); player_spawn_quick(); link_swap();          /* human 1 into the map */
}

void game_link_end(void) {
	if (link_saved_move >= 0) {
		if (link_phase) link_swap();
		opt_move = link_saved_move; opt_turn = link_saved_turn; opt_look = link_saved_look; opt_invert = link_saved_invert;
		opt_aim = link_saved_aim; difficulty = link_saved_diff; link_saved_move = -1;
	}
	link_on = 0; link_P = -1000; link_phase = 0;
}

void game_link_abort(const char *why) {
	if (!link_on) return;
	if (link_phase) link_swap();
	if (!match_over) match_end(why);
}

uint32_t game_link_hash(void) {
	uint32_t h = 2166136261u; int i;
#define HM(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
	HM(rngs); HM(game_ticks); HM(link_phase);
#ifdef LINK_DESYNC_TEST
	if (game_ticks > 600) HM(link_me);
#endif
	HM(pl.x); HM(pl.y); HM(pl.z); HM(pl.health); HM(pl.armour); HM(pl.gun); HM(pl.dead); HM(pl_frags);
	HM(hum_other.p.x); HM(hum_other.p.y); HM(hum_other.p.z); HM(hum_other.p.health); HM(hum_other.p.dead);
	for (i = 0; i < nmons; i++) if (i != link_P) { HM(mons[i].x); HM(mons[i].y); HM(mons[i].z); HM(mons[i].health); HM(mons[i].alive); HM(mons[i].enemy); }
	for (i = 0; i < MAXPROJ; i++) if (projs[i].life) { HM(projs[i].x); HM(projs[i].y); }
	HM(team_score[0]); HM(team_score[1]); HM(flags[0].state); HM(flags[1].state);
#undef HM
	return h;
}

/* the match menu's table: the humans first (this console's marked), then the bots */
int game_player_stat(int i, PlayerStat *s) {
	static const char *hname[2] = { "PLAYER 1", "PLAYER 2" };
	static const char *bname[8] = { "BOT 1", "BOT 2", "BOT 3", "BOT 4", "BOT 5", "BOT 6", "BOT 7", "BOT 8" };
	int nh = link_on ? 2 : 1, k, b = 0;
	memset(s, 0, sizeof(*s));
	if (i < nh) {
		if (!link_on || i == link_phase) {
			s->frags = pl_frags; s->deaths = pl.deaths; s->caps = pl_caps; s->team = pl_team;
		} else {
			s->frags = mons[link_P].frags; s->deaths = hum_other.p.deaths; s->caps = mons[link_P].caps; s->team = hum_other.team;
		}
		s->name = link_on ? hname[i] : "YOU"; s->you = !link_on || i == link_me;
		return 1;
	}
	for (k = 0; k < nmons; k++) if (mons[k].bot == 1) {
		if (b == i - nh) {
			s->name = bname[b & 7]; s->frags = mons[k].frags; s->deaths = mons[k].deaths; s->caps = mons[k].caps; s->team = mons[k].team;
			return 1;
		}
		b++;
	}
	return 0;
}
