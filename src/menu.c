/* Cube PSX front end: splash screen, the title menu over the attract demo, profiles (new / load), the map selector,
 * options (controls, sound) and the pause menu.  Runs at the 60 Hz game tick (menu_tick) and draws with the ui_* calls
 * in render.c (menu_draw).
 *
 *   NEW GAME  -> pick a card slot -> type a name -> pick the difficulty (the announcer calls it) -> MAP SELECT
 *   LOAD GAME -> pick a profile -> MAP SELECT
 *   MAP SELECT: Cube's campaigns; a map opens when the one before it is beaten.  A map with a game saved from the pause
 *               menu resumes it (SQUARE starts it over).
 *   A finished map is recorded in the profile (best time, all kills) and saved to the card, then START returns here. */
#include "cube.h"
#include <string.h>
#include <stdio.h>
#include <psxpad.h>
#include <psxgpu.h>

enum { ST_SPLASH, ST_TITLE, ST_PLAY, ST_PAUSE };
enum { M_MAIN, M_NEWSLOT, M_NAME, M_DIFF, M_LOAD, M_HUB, M_CAMPAIGNS, M_MAPS, M_MULTI, M_QUICK, M_LINK, M_OPTIONS, M_CONTROLS, M_SOUND, M_GAMEPLAY, M_PAUSE, M_MATCH, M_LOOK };
static int menu_state = ST_SPLASH, sub = M_MAIN, opt_back = M_MAIN, sel, t, fade_t, last_ly, last_lx;
static char slot_line[3][48]; static int slot_used[3], confirm_slot = -1, new_slot;
static const char *note; static int note_t;
static char name_buf[11]; static int name_len, kb_x, kb_y;
static int map_top, cur_cat = -1, cur_ep, recorded, next_cat = -1;

static const char *diff_names[3] = { "EASY", "NORMAL", "HARD" };
static const char *main_items[] = { "NEW GAME", "LOAD GAME", "OPTIONS" };
static const char *hub_items[] = { "SINGLE PLAYER", "QUICKPLAY", "MULTIPLAYER", "CHARACTER", "OPTIONS", "QUIT TO TITLE" };
/* the character: Giga Chad or Giga Stacy (portrait and voice) - chosen when a profile is made, changed from the hub */
static const char *char_names[2] = { "GIGA CHAD", "GIGA STACY" };
static int look_from_hub;
/* the face and the voice come off the disc through the buffer the title demo's map lives in: after a change the demo
   is stopped and hidden (demo_dirty) until it is loaded again */
static int demo_dirty;
static void apply_character(int c) {
	static int loaded;                              /* what is in VRAM and SPU RAM now (the dude, at start-up) */
	opt_char = c & 1;
	if (opt_char == loaded) return;
	render_set_face(opt_char); snd_set_voice(opt_char); loaded = opt_char;
	if (menu_state == ST_TITLE) demo_dirty = 1;
}

/* ---- quickplay: you against bots, a match on one map ---- */
static const struct { int8_t world; uint8_t ctf; const char *name; } qp_maps[] = {      /* ctf: 1 = has flags, 2 = has a hill */
	{ 15, 3, "FACING CUBES" }, { 0, 0, "PIG01 SURFACE TENSION" }, { 1, 0, "CRUEL01" }, { 4, 0, "CRUEL02" },
	{ 3, 0, "WSG1 WHY SO GREEN" }, { 2, 0, "PIG09 DARK HAND" }, { 5, 0, "PIG07" }, { 6, 0, "PIGFIN FINAL BREATH" },
	{ 25, 3, "CROSSFIRE" }, { 24, 0, "PUMPROOM" }, { 26, 2, "NIGHT YARD" } };        /* (the XENO pack's multiplayer maps) */
#define QP_NMAPS ((int)(sizeof(qp_maps) / sizeof(qp_maps[0])))
static const char *qp_mode_names[5] = { "", "DEATHMATCH", "TEAM DEATHMATCH", "CAPTURE THE FLAG", "KING OF THE HILL" };
#define QP_OK(mode, k) ((mode) == GM_CTF ? (qp_maps[k].ctf & 1) : (mode) == GM_KOTH ? (qp_maps[k].ctf & 2) : 1)
static int qp_mode = GM_CTF, qp_map, qp_bots = 3, qp_skill = 1;
static const char *multi_items[] = { "QUICKPLAY VS BOTS", "LINK CABLE", "BACK" };
/* ---- link cable: two consoles, a player each, bots if you like ---- */
static int lk_mode = GM_DM, lk_map, lk_bots = 2, lk_teams = LT_VERSUS, lk_skill = 1;
/* a multiplayer match's START menu: the table, and on a single console a pause (no options in a link game: both
   consoles must run the same game, and options change how you move) */
static const char *match_items[] = { "RESUME", "OPTIONS", "QUIT MATCH" };
static const char *link_match_items[] = { "RESUME", "QUIT MATCH (THE OTHER PLAYER WINS)" };
static int link_leaving;                       /* asked to leave a link game: frames since */
int menu_paused(void) { return menu_state == ST_PAUSE; }
static const char *pause_items[] = { "RESUME", "SAVE GAME", "OPTIONS", "QUIT TO MAP SELECT", "QUIT TO TITLE" };
static const char *options_items[] = { "CONTROLS", "SOUND", "GAMEPLAY", "BACK" };

/* the option pages: rows of sliders (lo..10) and ON/OFF switches, then BACK */
typedef struct { const char *label; int *v; int lo; } OptRow;      /* lo < 0: an ON/OFF switch */
typedef struct { const char *title; int n; OptRow r[5]; } OptPage;
static const OptPage opt_pages[3] = {
	{ "CONTROLS", 4, { { "MOVE SPEED", &opt_move, 1 }, { "TURN SPEED", &opt_turn, 1 }, { "LOOK SPEED", &opt_look, 1 }, { "INVERT LOOK", &opt_invert, -1 } } },
	{ "SOUND", 3, { { "SOUND VOLUME", &opt_sfx, 0 }, { "SPEECH VOLUME", &opt_speech, 0 }, { "MUSIC VOLUME", &opt_music, 0 } } },
	{ "GAMEPLAY", 1, { { "AIM ASSIST", &opt_aim, -1 } } },
};
#define IS_OPTPAGE(m) ((m) == M_CONTROLS || (m) == M_SOUND || (m) == M_GAMEPLAY)
#define OPTPAGE(m) (&opt_pages[(m) == M_CONTROLS ? 0 : (m) == M_SOUND ? 1 : 2])
static const char *kb_rows[4] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-.!?", "0123456789" };

int menu_world_visible(void) { return menu_state != ST_SPLASH && !demo_dirty; }
int menu_hud_visible(void) { return menu_state == ST_PLAY || menu_state == ST_PAUSE; }

void menu_init(void) { menu_state = ST_SPLASH; t = 0; }

static void say_note(const char *s) { note = s; note_t = 150; }

static void read_slots(void) { int i; for (i = 0; i < 3; i++) slot_used[i] = prof_describe(i, slot_line[i]); }

static void to_title(void) {
	void (*cb)(const char *) = world_loading_cb;
	world_loading_cb = 0;                    /* no LOADING text over the menu */
	demo_begin();
	world_loading_cb = cb;
	menu_state = ST_TITLE; sub = M_MAIN; sel = 0; fade_t = 0;
	music_play(0);
}
static void demo_reload(void) {                 /* the title demo from the start, the menu as it is (see apply_character) */
	void (*cb)(const char *) = world_loading_cb;
	if (!demo_dirty) return;
	world_loading_cb = 0; demo_begin(); world_loading_cb = cb;
	demo_dirty = 0;
}

/* ---- the map selector: one campaign's maps (cur_ep), a row each ---- */
#define MAP_ROWS (episodes[cur_ep].count)
#define MAP_VIS 8                            /* rows shown at once */
static int ep_of_cat(int c) { int e; for (e = 0; e < EP_N; e++) if (c >= episodes[e].first && c < episodes[e].first + episodes[e].count) return e; return 0; }
static int map_open(int c) { return c >= 0 && catalog[c].world >= 0 && (prof.maps[c].flags & MR_OPEN); }
static void ep_progress(int e, int *beaten, int *total) {
	int c; *beaten = *total = 0;
	for (c = episodes[e].first; c < episodes[e].first + episodes[e].count; c++)
		if (catalog[c].world >= 0) { ++*total; if (prof.maps[c].flags & MR_BEATEN) ++*beaten; }
}

static void to_maps(int cat) {                /* the list of cat's campaign, cat under the cursor */
	if (cat < 0) cat = 0;
	cur_ep = ep_of_cat(cat); sub = M_MAPS; sel = cat - episodes[cur_ep].first;
	map_top = sel - MAP_VIS / 2; if (map_top > MAP_ROWS - MAP_VIS) map_top = MAP_ROWS - MAP_VIS; if (map_top < 0) map_top = 0;
}
static void to_hub(void) { sub = M_HUB; sel = 0; }

static void start_map(int c, int resume) {
	demo_mode = 0; difficulty = prof.diff; cur_cat = c; recorded = 0; note_t = 0;
	music_for_map(c);
	if (resume && prof_resume()) { menu_state = ST_PLAY; return; }
	game_start(catalog[c].world);
	menu_state = ST_PLAY;
}

static int count(void) {
	switch (sub) {
	case M_MAIN: return 3;
	case M_HUB: return 6;
	case M_LOOK: return 2;
	case M_QUICK: return 6;
	case M_LINK: return 7;
	case M_CAMPAIGNS: return EP_N;
	case M_MULTI: return 3;
	case M_PAUSE: return 5;
	case M_MATCH: return link_on ? 2 : 3;
	case M_OPTIONS: return 4;
	case M_CONTROLS: case M_SOUND: case M_GAMEPLAY: return OPTPAGE(sub)->n + 1;
	case M_DIFF: return 3;
	case M_MAPS: return MAP_ROWS;
	default: return 3;
	}
}

/* d-pad, or the left stick pushed past halfway */
static int nav(int pressed, int ly) {
	int d = 0;
	if (pressed & PAD_UP) d = -1;
	if (pressed & PAD_DOWN) d = 1;
	if (ly < -80 && last_ly >= -80) d = -1;
	if (ly > 80 && last_ly <= 80) d = 1;
	last_ly = ly;
	return d;
}
static int nav_lr(int pressed, int lx) {
	int d = (pressed & PAD_RIGHT) ? 1 : (pressed & PAD_LEFT) ? -1 : 0;
	if (lx < -80 && last_lx >= -80) d = -1;
	if (lx > 80 && last_lx <= 80) d = 1;
	last_lx = lx;
	return d;
}

static void slider(int *v, int lr, int lo) { *v += lr; if (*v < lo) *v = lo; if (*v > 10) *v = 10; }

static void leave_options(void) {
	if (prof_slot >= 0) prof_save();          /* options live in the profile */
	sub = opt_back; sel = opt_back == M_HUB ? 4 : 2;
}

static void menu_input(int pressed, int lx, int ly) {
	int n = count(), d = nav(pressed, ly), lr = nav_lr(pressed, lx);
	int ok = pressed & (PAD_CROSS | PAD_START), back = pressed & (PAD_CIRCLE | PAD_TRIANGLE);
	switch (sub) {
	case M_MAIN:
		if (d) sel = (sel + d + n) % n;
		if (!ok) break;
		if (sel == 0) { read_slots(); sub = M_NEWSLOT; sel = 0; confirm_slot = -1; }
		else if (sel == 1) {
			read_slots(); sub = M_LOAD; sel = 0;
			if (!slot_used[0] && !slot_used[1] && !slot_used[2]) say_note("NO PROFILES FOUND - IS A MEMORY CARD IN SLOT 1?");
		}
		else { opt_back = M_MAIN; sub = M_OPTIONS; sel = 0; }
		break;

	case M_NEWSLOT:                           /* where the new profile goes */
		if (d) { sel = (sel + d + 3) % 3; confirm_slot = -1; }
		if (back) { sub = M_MAIN; sel = 0; break; }
		if (!ok) break;
		if (slot_used[sel] && confirm_slot != sel) { confirm_slot = sel; say_note("X AGAIN TO REPLACE THAT PROFILE"); break; }
		new_slot = sel; name_len = 0; name_buf[0] = 0; kb_x = kb_y = 0; sub = M_NAME; note_t = 0;
		break;

	case M_NAME: {                            /* on-screen keyboard: 4 rows of 10, then SPACE / DEL / DONE */
		int done = 0;
		if (d) { kb_y = (kb_y + d + 5) % 5; if (kb_y == 4 && kb_x > 2) kb_x = 2; }
		if (lr) { int w = kb_y == 4 ? 3 : 10; kb_x = (kb_x + lr + w) % w; }
		if (pressed & PAD_CIRCLE) { if (name_len) name_buf[--name_len] = 0; else { sub = M_NEWSLOT; sel = new_slot; } break; }
		if (pressed & PAD_START) done = 1;
		else if (pressed & PAD_CROSS) {
			if (kb_y < 4) { if (name_len < 10) { name_buf[name_len++] = kb_rows[kb_y][kb_x]; name_buf[name_len] = 0; } }
			else if (kb_x == 0) { if (name_len < 10 && name_len) { name_buf[name_len++] = ' '; name_buf[name_len] = 0; } }
			else if (kb_x == 1) { if (name_len) name_buf[--name_len] = 0; }
			else done = 1;
		}
		if (done) {
			while (name_len && name_buf[name_len - 1] == ' ') name_buf[--name_len] = 0;
			if (!name_len) { say_note("TYPE A NAME FIRST"); break; }
			sub = M_LOOK; sel = opt_char; look_from_hub = 0; note_t = 0;
		}
		break;
	}

	case M_LOOK:                                   /* GIGA CHAD / GIGA STACY: the portrait changes as you choose */
		if (d || lr) { sel ^= 1; apply_character(sel); snd_say(V_OHYEAH, 1); }
		if (back) { demo_reload(); if (look_from_hub) { sub = M_HUB; sel = 3; } else sub = M_NAME; break; }
		if (!ok) break;
		apply_character(sel); demo_reload();
		if (look_from_hub) { prof_save(); sub = M_HUB; sel = 3; }
		else { sub = M_DIFF; sel = 1; }
		break;

	case M_DIFF:
		if (d) { sel = (sel + d + 3) % 3; snd_say(V_CAKE + sel, 1); }
		if (back) { sub = M_LOOK; sel = opt_char; look_from_hub = 0; break; }
		if (!ok) break;
		difficulty = sel;
		say_note(prof_new(new_slot, name_buf, sel) ? "PROFILE SAVED" : "NO MEMORY CARD: PLAY ON, BUT NOTHING IS KEPT AFTER POWER OFF");
		to_hub();
		break;

	case M_LOAD:
		if (d) sel = (sel + d + 3) % 3;
		if (back) { sub = M_MAIN; sel = 1; break; }
		if (!ok) break;
		if (!slot_used[sel]) { say_note("THAT SLOT IS EMPTY"); break; }
		if (prof_load(sel)) { prof_card_ok = 1; apply_character(opt_char); demo_reload(); to_hub(); }
		else say_note("COULD NOT LOAD");
		break;

	case M_HUB:
		if (d) sel = (sel + d + n) % n;
		if (!ok) break;
		if (sel == 0) { sub = M_CAMPAIGNS; sel = prof.has_state ? ep_of_cat(prof.state_cat) : 0; }
		else if (sel == 1) { sub = M_QUICK; sel = 4; }
		else if (sel == 2) { sub = M_MULTI; sel = 0; }
		else if (sel == 3) { sub = M_LOOK; sel = opt_char; look_from_hub = 1; }
		else if (sel == 4) { opt_back = M_HUB; sub = M_OPTIONS; sel = 0; }
		else { sub = M_MAIN; sel = 0; }
		break;

	case M_CAMPAIGNS:
		if (d) sel = (sel + d + n) % n;
		if (back) { sub = M_HUB; sel = 0; break; }
		if (!ok) break;
		{   /* open the campaign at its saved game, else its first unbeaten open map */
			int e = sel, c = episodes[e].first, k;
			if (prof.has_state && ep_of_cat(prof.state_cat) == e) c = prof.state_cat;
			else for (k = episodes[e].first; k < episodes[e].first + episodes[e].count; k++)
				if (map_open(k) && !(prof.maps[k].flags & MR_BEATEN)) { c = k; break; }
			to_maps(c);
		}
		break;

	case M_QUICK:                                  /* MODE, MAP, BOTS, SKILL, START, BACK */
		if (d) sel = (sel + d + n) % n;
		if (back || (sel == 5 && ok)) { sub = M_HUB; sel = 1; break; }
		if (lr || (ok && sel < 4)) {
			int k = lr ? lr : 1;
			if (sel == 0) { qp_mode = (qp_mode - 1 + k + 4) % 4 + 1; if (!QP_OK(qp_mode, qp_map)) qp_map = 0; }
			if (sel == 1) do qp_map = (qp_map + k + QP_NMAPS) % QP_NMAPS; while (!QP_OK(qp_mode, qp_map));
			if (sel == 2) { qp_bots += k; if (qp_bots < 1) qp_bots = 7; if (qp_bots > 7) qp_bots = 1; }
			if (sel == 3) qp_skill = (qp_skill + k + 3) % 3;
		}
		if (ok && sel == 4) {
			demo_mode = 0; difficulty = qp_skill; cur_cat = -1; recorded = 1; note_t = 0;
			music_play(1 + VSync(-1) % 15);
			game_start_quick(qp_maps[qp_map].world, qp_mode, qp_bots);
			menu_state = ST_PLAY;
		}
		break;

	case M_MULTI:
		if (d) sel = (sel + d + n) % n;
		if (back || (sel == 2 && ok)) { sub = M_HUB; sel = 2; break; }
		if (ok && sel == 0) { sub = M_QUICK; sel = 4; }
		if (ok && sel == 1) { link_open(); sub = M_LINK; sel = 5; }
		break;

	case M_LINK:                                   /* MODE, MAP, BOTS, TEAMS, SKILL, START, BACK */
		if (d) sel = (sel + d + n) % n;
		if (back || (sel == 6 && ok)) { sub = M_MULTI; sel = 1; break; }
		if (link_state() != LS_IDLE) break;           /* (an offer is out) */
		if (lr || (ok && sel < 5)) {
			int k = lr ? lr : 1;
			if (sel == 0) { lk_mode = (lk_mode - 1 + k + 4) % 4 + 1; if (!QP_OK(lk_mode, lk_map)) lk_map = 0; }
			if (sel == 1) do lk_map = (lk_map + k + QP_NMAPS) % QP_NMAPS; while (!QP_OK(lk_mode, lk_map));
			if (sel == 2) { lk_bots += k; if (lk_bots < 0) lk_bots = 6; if (lk_bots > 6) lk_bots = 0; }
			if (sel == 3) lk_teams ^= 1;
			if (sel == 4) lk_skill = (lk_skill + k + 3) % 3;
		}
		if (ok && sel == 5) {
			LinkSetup s;
			if (!link_peer()) { say_note("NO OTHER CONSOLE ON THE CABLE"); break; }
			memset(&s, 0, sizeof(s));
			s.map = qp_maps[lk_map].world; s.mode = lk_mode; s.bots = lk_bots; s.teams = lk_teams; s.skill = lk_skill;
			s.move = opt_move; s.turn = opt_turn; s.look[0] = opt_look; s.invert[0] = opt_invert; s.aim[0] = opt_aim;
			s.seed = (uint32_t)VSync(-1) * 2654435761u + t;
			link_host(&s);
		}
		break;

	case M_MAPS: {
		int c;
		if (d) {
			sel = (sel + d + MAP_ROWS) % MAP_ROWS;
			if (sel < map_top) map_top = sel;
			if (sel > map_top + MAP_VIS - 1) map_top = sel - MAP_VIS + 1;
		}
		if (back) { sub = M_CAMPAIGNS; sel = cur_ep; break; }
		c = episodes[cur_ep].first + sel;
		if (!(pressed & (PAD_CROSS | PAD_START | PAD_SQUARE))) break;
		if (catalog[c].world < 0) { say_note("NOT PORTED YET - IT IS TOO BIG FOR NOW"); break; }
		if (!map_open(c)) { say_note("LOCKED - BEAT THE MAP BEFORE IT"); break; }
		start_map(c, !(pressed & PAD_SQUARE) && prof.has_state && prof.state_cat == c);
		break;
	}

	case M_OPTIONS:
		if (d) sel = (sel + d + n) % n;
		if (back || (sel == 3 && ok)) { leave_options(); break; }
		if (ok) { sub = sel == 0 ? M_CONTROLS : sel == 1 ? M_SOUND : M_GAMEPLAY; sel = 0; }
		break;

	case M_CONTROLS: case M_SOUND: case M_GAMEPLAY: {
		const OptPage *pg = OPTPAGE(sub);
		if (d) sel = (sel + d + n) % n;
		if (sel < pg->n) {
			const OptRow *r = &pg->r[sel];
			if (r->lo < 0) { if (lr || ok) *r->v = !*r->v; }
			else if (lr) slider(r->v, lr, r->lo);
		}
		if (back || (sel == pg->n && ok)) { sel = sub == M_CONTROLS ? 0 : sub == M_SOUND ? 1 : 2; sub = M_OPTIONS; }
		break;
	}

	case M_MATCH:
		if (d) sel = (sel + d + n) % n;
		if (back || (pressed & PAD_START)) { menu_state = ST_PLAY; break; }
		if (!ok) break;
		if (sel == 0) menu_state = ST_PLAY;
		else if (link_on) { link_quit(); link_leaving = 1; menu_state = ST_PLAY; }       /* gone once the other console has it */
		else if (sel == 1) { opt_back = M_MATCH; sub = M_OPTIONS; sel = 0; }
		else { to_title(); fade_t = 40; sub = M_QUICK; sel = 4; }
		break;

	case M_PAUSE:
		if (d) sel = (sel + d + n) % n;
		if (back || (pressed & PAD_START)) { menu_state = ST_PLAY; break; }
		if (!ok) break;
		if (sel == 0) menu_state = ST_PLAY;
		else if (sel == 1) {
			if (prof_slot < 0) say_note("NO PROFILE TO SAVE INTO");
			else if (game_mode != GM_SP) say_note("NO SAVING IN QUICKPLAY");
			else if (prof_save_game(cur_cat)) say_note("GAME SAVED");
			else say_note("NOT SAVED: NO MEMORY CARD");
		}
		else if (sel == 2) { opt_back = M_PAUSE; sub = M_OPTIONS; sel = 0; }
		else if (sel == 3) { int qp = game_mode != GM_SP; to_title(); fade_t = 40; if (qp) { sub = M_QUICK; sel = 4; } else to_maps(cur_cat); }   /* (to_title's demo resets the mode) */
		else { to_title(); }
		break;
	}
}

static void link_begin(void) {                  /* both consoles agreed: load the map, then wait for each other */
	LinkSetup s; int who;
	if (!link_agreed(&s, &who)) return;
	demo_mode = 0; cur_cat = -1; recorded = 1; note_t = 0;
	music_play(1 + s.seed % 15);
	game_start_link(&s, who);
	link_loaded();
	menu_state = ST_PLAY;
}
static void link_leave(void) {                  /* out of a link game, back to the link screen */
	const char *e;
	link_stop(); game_link_end();
	e = link_error();
	to_title(); fade_t = 40; sub = M_LINK; sel = 5;
	if (e) say_note(e);
}

void menu_tick(int held, int pressed, int lx, int ly, int rx, int ry) {
	t++; fade_t++;
	if (note_t > 0) note_t--;
	if (menu_state == ST_TITLE && sub == M_LINK) {
		LinkSetup s; static const char *last_err;
		link_listen();
		if (link_error() && link_error() != last_err) say_note(link_error());
		last_err = link_error();
		if (link_state() == LS_IDLE) link_offer(&s);   /* the other console's game: join it */
		link_begin();
	}
	switch (menu_state) {
	case ST_SPLASH:
		if (t > 30 && (pressed & (PAD_START | PAD_CROSS)) && t < 230) t = 230;   /* skip */
		if (t >= 290) to_title();
		break;
	case ST_TITLE:
		if (!demo_dirty) game_tick(0, 0, 0, 0, 0, 0);  /* the demo plays on */
		if (demo_done) { int s = sub, k = sel, top = map_top; to_title(); fade_t = 40; sub = s; sel = k; map_top = top; }
		if (fade_t > 40) menu_input(pressed, lx, ly);
		break;
	case ST_PLAY:
		if (link_on) {                            /* the cable runs the game (main.c): START is the match menu (or back in, when fragged) */
			if (link_leaving && (match_over || ++link_leaving > 180 || link_state() != LS_PLAY)) { link_leaving = 0; link_leave(); break; }
			if (link_leaving) break;
			if (level_done && (pressed & PAD_START)) { link_leave(); break; }
			if (!level_done && (pressed & PAD_START) && !game_listener()->dead) { menu_state = ST_PAUSE; sub = M_MATCH; sel = 0; }
			break;
		}
		if (level_done && game_mode != GM_SP) {   /* a quickplay match is over: START goes back to the quickplay screen */
			if (pressed & PAD_START) { to_title(); fade_t = 40; sub = M_QUICK; sel = 4; }
			game_tick(held, 0, lx, ly, rx, ry);
			break;
		}
		if (level_done) {                         /* finished: record it once, then START goes back to the map list */
			if (!recorded) {
				int nx;
				recorded = 1;
				nx = prof_complete(cur_cat, level_done == 2, level_secs, pl.kills);
				next_cat = nx >= 0 ? nx : cur_cat;
				if (prof_card_ok) say_note(nx >= 0 ? "PROGRESS SAVED - NEXT MAP OPEN" : "PROGRESS SAVED");
				else say_note(nx >= 0 ? "NEXT MAP OPEN - NOT SAVED: NO MEMORY CARD" : "NOT SAVED: NO MEMORY CARD");
			}
			if (pressed & PAD_START) { to_title(); fade_t = 40; to_maps(next_cat); }
			game_tick(held, 0, lx, ly, rx, ry);
			break;
		}
		if ((pressed & PAD_START) && !pl.dead) { menu_state = ST_PAUSE; sub = game_mode == GM_SP ? M_PAUSE : M_MATCH; sel = 0; break; }
		game_tick(held, pressed, lx, ly, rx, ry);
		break;
	case ST_PAUSE:
		if (link_on && level_done) { menu_state = ST_PLAY; break; }   /* (the match ended under the menu) */
		menu_input(pressed, lx, ly);
		break;
	}
}

/* ---------------- drawing ---------------- */
#define GOLD  0x406080          /* text tints (r | g << 8 | b << 16; 128 = the font's own white) */
#define WHITE 0x808080
#define DIM   0x405060
#define DARK  0x283038
#define GREEN 0x50a060

static void centre(int y, const char *s, int size, uint32_t col) { ui_text(160 - ui_text_width(s, size) / 2, y, s, size, col); }

static uint32_t scale_col(uint32_t c, int f) {    /* f 0..256 */
	return ((((c & 0xff) * f) >> 8)) | (((((c >> 8) & 0xff) * f) >> 8) << 8) | (((((c >> 16) & 0xff) * f) >> 8) << 16);
}

static void cursor(int x, int y, uint32_t c) { ui_logo(x, y, 14, t * 40, c); }     /* Quake's spinning cursor */

static void draw_list(const char *title, const char *const *items, int n, int y0, int bright, int size) {
	int i;
	ui_logo(160 - ui_text_width(title, 24) / 2 - 18, y0 + 11, 22, t * 24, scale_col(WHITE, bright));
	ui_text(160 - ui_text_width(title, 24) / 2, y0, title, 24, scale_col(GOLD, bright));
	for (i = 0; i < n; i++) {
		int y = y0 + 38 + i * (size + 4), w = ui_text_width(items[i], size);
		ui_text(160 - w / 2, y, items[i], size, scale_col(i == sel ? WHITE : DIM, bright));
		if (i == sel) cursor(160 - w / 2 - 14, y + size / 2, scale_col(WHITE, bright));
	}
}

static void draw_bar(int x, int y, int v) {
	int i;
	for (i = 0; i < 10; i++) ui_rect(x + i * 7, y, x + i * 7 + 5, y + 10, i < v ? 0xa0c060 : 0x303030);
}

static void draw_optpage(const OptPage *pg, int y0) {
	int i, step = pg->n > 3 ? 19 : 22;
	centre(y0, pg->title, 24, GOLD);
	for (i = 0; i <= pg->n; i++) {
		int y = y0 + 40 + i * step; uint32_t c = i == sel ? WHITE : DIM;
		if (i == pg->n) { centre(y, "BACK", 16, c); if (i == sel) cursor(160 - ui_text_width("BACK", 16) / 2 - 14, y + 8, c); continue; }
		ui_text(52, y + 1, pg->r[i].label, 14, c);
		if (i == sel) cursor(40, y + 8, c);
		if (pg->r[i].lo < 0) ui_text(198, y + 1, *pg->r[i].v ? "ON" : "OFF", 14, c);
		else draw_bar(198, y + 3, *pg->r[i].v);
	}
}

static void draw_slots(const char *title, int y0) {
	int i;
	centre(y0, title, 24, GOLD);
	for (i = 0; i < 3; i++) {
		int y = y0 + 40 + i * 20;
		ui_text(56, y, slot_line[i], 14, i == sel ? WHITE : (slot_used[i] ? DIM : DARK));
		if (i == sel) cursor(44, y + 7, WHITE);
	}
}

static void draw_name(int y0) {
	int r, k; char b[16];
	centre(y0, "YOUR NAME", 24, GOLD);
	sprintf(b, "%s%s", name_buf, (t >> 4) & 1 ? "_" : " ");
	ui_rect(80, y0 + 32, 240, y0 + 52, 0x101418);
	centre(y0 + 34, b, 16, WHITE);
	for (r = 0; r < 4; r++)
		for (k = 0; k < 10; k++) {
			int x = 70 + k * 18, y = y0 + 62 + r * 18; char ch[2] = { kb_rows[r][k], 0 };
			if (kb_y == r && kb_x == k) ui_rect(x - 3, y - 2, x + 13, y + 15, 0x405870);
			ui_text(x + 5 - ui_text_width(ch, 14) / 2, y, ch, 14, kb_y == r && kb_x == k ? WHITE : DIM);
		}
	for (k = 0; k < 3; k++) {
		static const char *w[3] = { "SPACE", "DEL", "DONE" };
		int x = 76 + k * 60, y = y0 + 136;
		if (kb_y == 4 && kb_x == k) ui_rect(x - 4, y - 2, x + 50, y + 15, 0x405870);
		ui_text(x + 23 - ui_text_width(w[k], 12) / 2, y + 1, w[k], 12, kb_y == 4 && kb_x == k ? WHITE : DIM);
	}
}

static void draw_diff(int y0) {
	static const char *sub_text[3] = { "MONSTERS HIT SOFTER AND THINK SLOWER", "CUBE AS IT WAS MEANT TO BE PLAYED", "FASTER, MEANER, HARDER HITTING" };
	int i;
	centre(y0, "DIFFICULTY", 24, GOLD);
	for (i = 0; i < 3; i++) {
		int y = y0 + 40 + i * 22, w = ui_text_width(diff_names[i], 16);
		ui_text(160 - w / 2, y, diff_names[i], 16, i == sel ? WHITE : DIM);
		if (i == sel) cursor(160 - w / 2 - 14, y + 8, WHITE);
	}
	centre(y0 + 110, sub_text[sel], 10, DIM);
}

static void draw_header(const char *title, int y0) {
	char b[64];
	sprintf(b, "%s  -  %s%s", prof.name, diff_names[prof.diff % 3], prof_card_ok ? "" : "  -  NOT SAVED (NO CARD)");
	centre(y0, title, 20, GOLD);
	centre(y0 + 22, b, 10, DIM);
}

static void draw_campaigns(int y0) {
	int e; char b[48];
	draw_header("SINGLE PLAYER", y0);
	for (e = 0; e < EP_N; e++) {
		int y = y0 + 44 + e * 30, bt, tot; uint32_t col = e == sel ? WHITE : DIM;
		ep_progress(e, &bt, &tot);
		ui_text(52, y, episodes[e].name, 14, col);
		sprintf(b, "BY %s", episodes[e].author); ui_text(52, y + 15, b, 10, e == sel ? DIM : DARK);
		sprintf(b, "%d / %d", bt, tot);
		ui_text(284 - ui_text_width(b, 12), y + 1, b, 12, bt == tot && tot ? (e == sel ? WHITE : GREEN) : col);
		if (prof.has_state && ep_of_cat(prof.state_cat) == e) ui_text(284 - ui_text_width("SAVED GAME", 9), y + 16, "SAVED GAME", 9, e == sel ? DIM : DARK);
		if (e == sel) cursor(40, y + 7, WHITE);
	}
}

static void draw_quick(int y0) {
	static const char *rows[6] = { "MODE", "MAP", "BOTS", "SKILL", "START", "BACK" };
	int i; char b[40];
	centre(y0, "QUICKPLAY", 20, GOLD);
	centre(y0 + 22, "YOU AGAINST BOTS - YOU ARE ON THE RED TEAM", 9, DIM);
	for (i = 0; i < 6; i++) {
		int y = y0 + 40 + i * 19; uint32_t c = i == sel ? WHITE : DIM;
		if (i >= 4) { centre(y, rows[i], 15, c); if (i == sel) cursor(160 - ui_text_width(rows[i], 15) / 2 - 14, y + 7, c); continue; }
		ui_text(46, y + 1, rows[i], 13, c);
		if (i == sel) cursor(36, y + 7, c);
		if (i == 0) strcpy(b, qp_mode_names[qp_mode]);
		else if (i == 1) strcpy(b, qp_maps[qp_map].name);
		else if (i == 2) { if (qp_mode == GM_DM) sprintf(b, "%d", qp_bots); else sprintf(b, "%d   (YOU + %d V %d)", qp_bots, qp_bots / 2, (qp_bots + 1) / 2); }
		else strcpy(b, diff_names[qp_skill]);
		ui_text(118, y + 1, b, 12, c);
	}
}

static const char *my_team_name(void) { return pl_team ? " - BLUE TEAM" : " - RED TEAM"; }
static void draw_link(int y0) {
	static const char *rows[7] = { "MODE", "MAP", "BOTS", "TEAMS", "SKILL", "START", "BACK" };
	int i; char b[48];
	centre(y0, "LINK CABLE", 20, GOLD);
	if (link_state() == LS_ASK) centre(y0 + 22, "WAITING FOR THE OTHER CONSOLE TO JOIN", 9, WHITE);
	else if (link_peer()) centre(y0 + 22, "OTHER CONSOLE FOUND: EITHER OF YOU CAN START", 9, 0x40c040);
	else centre(y0 + 22, "NO OTHER CONSOLE YET - CHECK THE CABLE", 9, (t / 30) & 1 ? WHITE : DIM);
	for (i = 0; i < 7; i++) {
		int y = y0 + 38 + i * 17; uint32_t c = i == sel ? WHITE : DIM;
		if (i >= 5) { centre(y, rows[i], 14, c); if (i == sel) cursor(160 - ui_text_width(rows[i], 14) / 2 - 14, y + 7, c); continue; }
		ui_text(46, y + 1, rows[i], 12, c);
		if (i == sel) cursor(36, y + 7, c);
		if (i == 0) strcpy(b, qp_mode_names[lk_mode]);
		else if (i == 1) strcpy(b, qp_maps[lk_map].name);
		else if (i == 2) sprintf(b, "%d", lk_bots);
		else if (i == 3) strcpy(b, lk_mode == GM_DM ? "EVERYONE FOR THEMSELVES" : lk_teams == LT_VERSUS ? "P1 RED V P2 BLUE" : "P1 AND P2 RED");
		else strcpy(b, diff_names[lk_skill]);
		ui_text(118, y + 1, b, 11, c);
	}
}

/* the match menu: who has how many, best first (by team in team games), and the choices */
static void draw_match(void) {
	PlayerStat ps[10]; int n = 0, i, j, y, ctf = game_mode == GM_CTF, teams = game_mode != GM_DM; char b[48];
	while (n < 10 && game_player_stat(n, &ps[n])) n++;
	for (i = 1; i < n; i++) for (j = i; j > 0; j--) {      /* (insertion sort: team, then frags, then flags) */
		PlayerStat *a = &ps[j - 1], *c = &ps[j]; PlayerStat tmp;
		int ka = (teams ? a->team * 10000 : 0) - a->frags * 10 - a->caps * 100, kc = (teams ? c->team * 10000 : 0) - c->frags * 10 - c->caps * 100;
		if (kc >= ka) break;
		tmp = *a; *a = *c; *c = tmp;
	}
	centre(5, qp_mode_names[game_mode], 16, GOLD);
	if (teams) {
		sprintf(b, "RED %d", team_score[0]); ui_text(150 - ui_text_width(b, 12), 24, b, 12, 0x3838c0);
		centre(24, "-", 12, DIM);
		sprintf(b, "BLUE %d", team_score[1]); ui_text(170, 24, b, 12, 0xc06038);
		sprintf(b, "TO %d", match_limit); ui_text(250, 27, b, 8, DIM);
	} else { sprintf(b, "FIRST TO %d FRAGS", match_limit); centre(26, b, 9, DIM); }
	y = 42;
	ui_text(40, y, "PLAYER", 8, DIM); ui_text(150, y, "FRAGS", 8, DIM); ui_text(200, y, "DEATHS", 8, DIM);
	if (ctf) ui_text(256, y, "FLAGS", 8, DIM);
	for (i = 0; i < n; i++) {
		uint32_t c = !teams ? (ps[i].you ? WHITE : DIM) : ps[i].team ? (ps[i].you ? 0xf08050 : 0xa05038) : (ps[i].you ? 0x5050f0 : 0x3838a0);
		y = 53 + i * 11;
		if (ps[i].you) cursor(30, y + 4, c);
		ui_text(40, y, ps[i].name, 9, c);
		sprintf(b, "%d", ps[i].frags); ui_text(160, y, b, 9, c);
		sprintf(b, "%d", ps[i].deaths); ui_text(212, y, b, 9, c);
		if (ctf) { sprintf(b, "%d", ps[i].caps); ui_text(266, y, b, 9, c); }
	}
	y = 53 + n * 11 + 5;
	{
		const char *const *it = link_on ? link_match_items : match_items; int ni = link_on ? 2 : 3;
		for (i = 0; i < ni; i++) {
			int yy = y + i * 13, w = ui_text_width(it[i], 11);
			ui_text(160 - w / 2, yy, it[i], 11, i == sel ? WHITE : DIM);
			if (i == sel) cursor(160 - w / 2 - 14, yy + 5, WHITE);
		}
	}
}

static void draw_look(void) {
	int i;
	centre(26, "CHOOSE YOUR CHARACTER", 16, GOLD);
	for (i = 0; i < 2; i++) {
		int x = i ? 216 : 104, w = ui_text_width(char_names[i], 12);
		ui_text(x - w / 2, 160, char_names[i], 12, i == sel ? WHITE : DIM);
		if (i == sel) cursor(x - w / 2 - 14, 166, WHITE);
	}
	render_face_preview(sel ? 172 : 60, 50);               /* (only the chosen face is loaded) */
	centre(178, sel ? "THE QUEEN OF THE ARENA" : "THE KING OF THE ARENA", 9, DIM);
}

static void draw_maps(int y0) {
	int r; char b[64];
	const Episode *ep = &episodes[cur_ep];
	draw_header(ep->name, y0);
	for (r = 0; r < MAP_VIS && map_top + r < MAP_ROWS; r++) {
		int row = map_top + r, c = ep->first + row, y = y0 + 40 + r * 15;
		const CatMap *m = &catalog[c]; const MapRec *rec = &prof.maps[c];
		int open = map_open(c), ported = m->world >= 0;
		uint32_t col = row == sel ? WHITE : !ported ? DARK : open ? DIM : DARK;
		char st[24];
		if (!ported) strcpy(st, "NOT PORTED");
		else if (!open) strcpy(st, "LOCKED");
		else if (prof.has_state && prof.state_cat == c) strcpy(st, "SAVED GAME");
		else if (rec->flags & MR_BEATEN) sprintf(st, "%s%d:%02d", rec->flags & MR_ALLKILLS ? "* " : "", rec->best_secs / 60, rec->best_secs % 60);
		else strcpy(st, "NEW");
		{   /* the title gets what room the status leaves */
			int room = 284 - ui_text_width(st, 11) - 10 - 52, n;
			sprintf(b, "%s  %s", m->name, m->title);
			n = strlen(b);
			if (ui_text_width(b, 12) > room) { while (n > 4 && (b[n] = 0, ui_text_width(b, 12) + ui_text_width("..", 12) > room)) n--; strcat(b, ".."); }
		}
		ui_text(52, y, b, 12, col);
		if (row == sel) cursor(44, y + 6, WHITE);
		ui_text(284 - ui_text_width(st, 11), y + 1, st, 11, (rec->flags & MR_BEATEN) && ported ? (row == sel ? WHITE : GREEN) : col);
	}
	if (map_top > 0) centre(y0 + 34, "^", 10, DIM);
	if (map_top + MAP_VIS < MAP_ROWS) centre(y0 + 40 + MAP_VIS * 15 - 2, "v", 10, DIM);
}

void menu_draw(void) {
	int f, wide;
	if (menu_state == ST_SPLASH) {
		/* fade in 0..60, hold, fade out 230..290 */
		f = t < 60 ? t * 256 / 60 : t > 230 ? (290 - t) * 256 / 60 : 256;
		if (f < 0) f = 0;
		ui_logo(160, 74, 112, 0, scale_col(WHITE, f));
		centre(140, "CUBE PSX", 32, scale_col(GOLD, f));
		centre(184, "RUNNING THE CUBE ENGINE", 12, scale_col(WHITE, f));
		centre(200, "BY WOUTER VAN OORTMERSSEN", 10, scale_col(DIM, f));
		return;
	}
	if (menu_state == ST_PLAY) {
		if (link_on && link_state() == LS_SYNC) { centre(100, "WAITING FOR THE OTHER CONSOLE", 13, (t / 30) & 1 ? WHITE : DIM); return; }
		if (link_on && qp_waiting) {               /* the link warm-up: both must be ready */
			char b[48];
			if (game_link_ready(link_me)) { sprintf(b, "READY - WAITING FOR PLAYER %d", 2 - link_me); centre(96, b, 13, WHITE); }
			else centre(96, "PRESS FIRE WHEN YOU ARE READY", 15, (game_ticks / 30) & 1 ? WHITE : DIM);
			sprintf(b, "YOU ARE PLAYER %d%s", link_me + 1, game_mode == GM_DM ? "" : my_team_name());
			centre(116, b, 10, DIM);
			centre(132, qp_mode_names[game_mode], 9, DIM);
			centre(4, "LINK WARM-UP", 11, WHITE);
			return;
		}
		if (game_mode != GM_SP && qp_waiting) {    /* the warm-up */
			centre(96, "PRESS FIRE TO START", 15, (game_ticks / 30) & 1 ? WHITE : DIM);
			centre(116, qp_mode_names[game_mode], 10, DIM);
			centre(4, "WARM-UP", 11, WHITE);
			centre(132, "FLY ANYWHERE: GO WHERE YOU LOOK, X TO RISE", 9, DIM);
			return;
		}
		if (game_mode != GM_SP) {                  /* the score */
			char b[48];
			if (game_mode == GM_DM && link_on) {
				int o = 1 - link_me;
				if (nmons > 1) sprintf(b, "FRAGS %d   PLAYER %d: %d   BEST BOT %d", pl_frags, o + 1, game_link_frags(o), qp_leader_frags());
				else sprintf(b, "FRAGS %d   PLAYER %d: %d", pl_frags, o + 1, game_link_frags(o));
				centre(4, b, 11, WHITE);
			}
			else if (game_mode == GM_DM) { sprintf(b, "FRAGS %d   BEST BOT %d", pl_frags, qp_leader_frags()); centre(4, b, 11, WHITE); }
			else {
				sprintf(b, "RED %d", team_score[0]); ui_text(110 - ui_text_width(b, 13), 4, b, 13, 0x2828d0);
				sprintf(b, "BLUE %d", team_score[1]); ui_text(210, 4, b, 13, 0xd05028);
				sprintf(b, "TO %d", match_limit); centre(6, b, 9, DIM);
			}
			if (game_mode == GM_CTF && flags[1 - pl_team].state == 1 && flags[1 - pl_team].carrier == -1) centre(170, "YOU HAVE THE FLAG - TAKE IT HOME", 10, WHITE);
			if (game_mode == GM_KOTH) { int hx, hy, hz, ho;
				if (koth_hill(&hx, &hy, &hz, &ho)) centre(32, ho == 0 ? "THE HILL: RED" : ho == 1 ? "THE HILL: BLUE" : ho == 2 ? "THE HILL: CONTESTED" : "THE HILL: NOBODY", 9, ho == 0 ? 0x2828d0 : ho == 1 ? 0xd05028 : DIM); }
		}
		if (note_t > 0) centre(20, note, 12, WHITE);
		return;
	}
	if (menu_state == ST_TITLE && fade_t < 60) ui_dim(0, 0, 320, 240, 255 - fade_t * 255 / 60);     /* the demo fades in from black */
	f = menu_state == ST_TITLE ? (fade_t < 40 ? 0 : fade_t > 80 ? 256 : (fade_t - 40) * 256 / 40) : 256;
	wide = !(sub == M_MAIN || sub == M_PAUSE || sub == M_OPTIONS || sub == M_HUB);
	if (sub == M_MATCH) ui_dim(16, 2, 304, 172, 175);      /* (above the status bar) */
	else if (sub == M_LOOK) ui_dim(28, 18, 292, 192, f * 160 >> 8);
	else if (sub == M_MAPS || sub == M_NAME || sub == M_CAMPAIGNS) ui_dim(20, 16, 300, 180, f * 170 >> 8);
	else if (wide) ui_dim(28, 22, 292, 166, f * 150 >> 8);
	else ui_dim(68, 22, 252, 152 + (sub == M_PAUSE ? 14 : sub == M_HUB ? 26 : 0), f * 140 >> 8);     /* narrow: let the demo show */
	switch (sub) {
	case M_MAIN: draw_list("CUBE PSX", main_items, 3, 34, f, 16); break;
	case M_PAUSE: draw_list("PAUSED", pause_items, 5, 30, f, 14); break;
	case M_MATCH: draw_match(); break;
	case M_OPTIONS: draw_list("OPTIONS", options_items, 4, 34, f, 16); break;
	case M_CONTROLS: case M_SOUND: case M_GAMEPLAY: draw_optpage(OPTPAGE(sub), 34); break;
	case M_NEWSLOT: draw_slots("NEW PROFILE", 34); break;
	case M_LOAD: draw_slots("LOAD PROFILE", 34); break;
	case M_NAME: draw_name(22); break;
	case M_DIFF: draw_diff(34); break;
	case M_MAPS: draw_maps(20); break;
	case M_HUB: draw_list(prof.name[0] ? prof.name : "CUBE PSX", hub_items, 6, 26, f, 14); break;
	case M_LOOK: draw_look(); break;
	case M_QUICK: draw_quick(20); break;
	case M_LINK: draw_link(20); break;
	case M_CAMPAIGNS: draw_campaigns(20); break;
	case M_MULTI: draw_list("MULTIPLAYER", multi_items, 3, 34, f, 15); break;
	}
	{
		int hy = menu_state == ST_TITLE ? 196 : 182;   /* on the title screen the status bar strip is free */
		const char *h = "X SELECT   O BACK";
		if (sub == M_MAIN) h = "X SELECT";
		else if (sub == M_PAUSE) h = "X SELECT   START RESUME";
		else if (sub == M_MATCH) h = "";
		else if (IS_OPTPAGE(sub)) h = "LEFT / RIGHT CHANGE   O BACK";
		else if (sub == M_NAME) h = "X TYPE   O DELETE   START DONE";
		else if (sub == M_HUB) h = "X SELECT";
		else if (sub == M_LOOK) h = "LEFT / RIGHT CHOOSE   X OK   O BACK";
		else if (sub == M_QUICK || sub == M_LINK) h = "LEFT / RIGHT CHANGE   X START   O BACK";
		else if (sub == M_CAMPAIGNS) h = "X OPEN   O BACK";
		else if (sub == M_MAPS) h = prof.has_state && episodes[cur_ep].first + sel == prof.state_cat ? "X RESUME   SQUARE START OVER   O BACK" : "X PLAY   O BACK";
		if (note_t > 0) centre(hy, note, 10, WHITE);           /* a message takes the hint line for a moment */
		else centre(hy, h, 10, scale_col(DIM, f));
		if (menu_state == ST_TITLE) centre(216, "CUBE ENGINE BY WOUTER VAN OORTMERSSEN", 9, scale_col(DARK, f));
	}
}
