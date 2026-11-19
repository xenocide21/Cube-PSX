/* Cube PSX - shared definitions.
 * Port of Cube (Wouter van Oortmerssen, zlib license) game rules to the PlayStation 1.
 * All world coordinates are fixed point: 1 cube unit = 256 (FX). Cube "z" is up.
 */
#ifndef CUBEPSX_H
#define CUBEPSX_H
#include <sys/types.h>
#include <stdint.h>

#define FX 256
#define SOLID_T 0
#define CORNER_T 1
#define FHF_T 2
#define CHF_T 3
#define SPACE_T 4

/* A map square, exactly as it is stored in the .cmp file (12 bytes), so the map is used where it is loaded - no second
 * copy.  Level 0 of the mip hierarchy is the map itself; the higher levels (Sqr below) use the same layout, with the tag
 * byte holding `defer` instead (only real cells have door tags). */
typedef struct {                    /* 12 bytes, word aligned: the map file's own cell layout */
	uint8_t type; int8_t floor, ceil;
	uint8_t wtex, ftex, ctex, utex, vdelta;
	union {
		struct { union { uint8_t tag; uint8_t defer; }; uint8_t r, g, b; };
		uint32_t tl;                    /* tag | r<<8 | g<<16 | b<<24 */
	};
} Cell;
typedef Cell Sqr;
_Static_assert(sizeof(Cell) == 12, "cell layout must match the map file");
#define SQLIGHT(s) ((s)->tl >> 8)

typedef struct __attribute__((packed)) {
	int16_t x, y, z, attr1; uint8_t type, attr2, attr3, attr4;
} MapEnt;

/* entity types, same numbering as Cube */
enum { E_NOTUSED, E_LIGHT, E_PLAYERSTART, E_SHELLS, E_BULLETS, E_ROCKETS, E_ROUNDS,
       E_HEALTH, E_BOOST, E_GREENARMOUR, E_YELLOWARMOUR, E_QUAD, E_TELEPORT, E_TELEDEST,
       E_MAPMODEL, E_MONSTER, E_CARROT, E_JUMPPAD,
       E_FLAG = 18,             /* ours (Facing Cubes): a team's flag base, attr2 = team (0 red, 1 blue) */
       E_CLIPS = 19,            /* ours: a box of pistol ammo (gives the pistol, like Cube's ammo boxes give their guns) */
       E_HILL = 20 };           /* ours: King of the Hill's hill, centred here; attr1 / attr2 = half its size in x / y, z = its floor */

enum { GUN_FIST, GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE, GUN_FIREBALL, GUN_ICEBALL, GUN_SLIMEBALL, GUN_BITE, GUN_PISTOL, NUMGUNS };   /* pistol: ours */

/* ---- cdload.c: files live on the disc; these buffers hold the current map ---- */
int  cd_init(void);
int  cd_load(const char *name, void *buf, int bufsize);
/* One memory area for the map and the texture pack: a map's .tex is loaded first and goes straight to video memory, then
 * its .cmp is loaded over it and the mip levels are built after the cells.  Big enough for a 256x256 map (786 KB of cells,
 * 262 KB of mips, header and entities); also the CD buffer for MODELS.PAK, SKY.PAK and the sound banks at start-up. */
#define TEXBUF_SZ (524 * 2048)
extern uint32_t g_texbuf[TEXBUF_SZ / 4];
void world_forget(void);                     /* call after loading anything else into g_texbuf (it held the map) */

/* ---- world.c ---- */
/* The renderer's view of the world is Cube's own structure: level 0 is the map; each higher mip level halves the grid and
 * summarises 2x2 squares, exactly as Cube's remip() does. */
#define SEMISOLID_T 5
#define MAXMIPS 8
extern Sqr *wmip[MAXMIPS + 1];               /* wmip[n] has (ssize >> n)^2 squares, row-major */
extern int world_sfactor;                    /* ssize = 1 << sfactor */
extern int8_t *world_htop[MAXMIPS + 1];      /* highest point drawn in each square, per mip level (0 if no room) */
void world_remip(int x, int y, int xs, int ys);   /* rebuild the mips above the level-0 rectangle after a cell change */
void world_settag(int tag, int type);       /* Cube's settag: open the cells of `tag`, or (tag 0) reset all tagged cells */

extern void (*world_loading_cb)(const char *what);   /* optional: called before a (slow) disc read */
int  world_load(int mapindex);
int  world_ssize(void);
int  world_sky(void);
uint8_t *world_spare(int *size);              /* free memory after the loaded map, until the next load */                         /* which sky box: 0 Cube's, 1 space (Facing Cubes) */
int  world_generation(void);                 /* bumps on every world_load */
extern void (*world_tex_cb)(void);           /* called by world_load with the texture pack in memory (world_texpack) */
const uint8_t *world_texpack(void);          /* TEX2 pack of the current map */
const char *world_title(void);
#define NO_WATER (-100000)
int  world_water(void);                      /* Cube's waterlevel (units), or NO_WATER */
const Cell *world_cell(int cx, int cy);          /* out of range -> solid sentinel */
int  world_nents(void);
/* the automap's memory of where you have been: one bit per 2x2 cells of a 256 map (render.c marks what it draws, the
   game marks around the player; cleared by world_load, kept in saved games) */
extern uint8_t world_seen[2048];
#define SEEN_IDX(cx, cy) ((((cy) >> 1) << 4) | ((cx) >> 4))
#define SEEN_BIT(cx, cy) (1 << (((cx) >> 1) & 7))
#define SEEN_MARK(cx, cy) do { uint8_t *w_ = &world_seen[SEEN_IDX(cx, cy)]; int b_ = SEEN_BIT(cx, cy); if (!(*w_ & b_)) { *w_ |= b_; seen_gen++; } } while (0)
extern int seen_gen;                     /* bumps whenever something new is seen (the automap rebuilds) */
#define SEEN(cx, cy) (world_seen[SEEN_IDX(cx, cy)] & SEEN_BIT(cx, cy))
extern int automap_on;                   /* SELECT: the full-screen map instead of the view (game.c) */
const MapEnt *world_ent(int i);
/* map props (Cube's mapmodels): where each stands and how big it is for collision (Cube's mmcollide: a box of rad around
   its spot, from z to z + h; below it you meet a ceiling, above it a floor).  mdl = Cube's mapmodel slot 0-8 */
typedef struct { int32_t x, y, z; int16_t rad, h; uint16_t yaw; uint8_t mdl, pad; } MapModel;
#define MAXMM 160
#define NUM_MAPMODELS 11                 /* Cube's slots 0-8, then ours: 9 the CTF flag, 10 the pistol ammo box */
extern MapModel world_mm[MAXMM];
extern int world_nmm;
/* can a body of radius rad stand at (x,y) with feet at zfeet? returns 0 if blocked,
 * else 1 and fills *floorz (highest floor under body, FX) and *ceilz (lowest ceiling, FX) */
int  world_fits(int x, int y, int rad, int zfeet, int stepmax, int *floorz, int *ceilz);
/* ray march: from (x,y,z) along dir (4.12 unit vector) until world hit or maxdist (FX); returns dist (FX) */
int  world_ray(int x, int y, int z, int dx, int dy, int dz, int maxdist);
int  world_los(int x0, int y0, int z0, int x1, int y1, int z1);

/* ---- game.c ---- */
typedef struct {
	int x, y, z;            /* z = eye height (FX) */
	int vx, vy, vz;
	int yaw, pitch;         /* 4096 = full turn */
	int health, armour, armourtype;
	int gun, gunwait, fireflash;
	int ammo[NUMGUNS];
	int onfloor, dead, hurt;
	int kills, deaths;
	int inwater;
} Player;

typedef struct {
	int x, y, z;            /* z = eye height */
	int yaw, type, health;
	int mstate;             /* Cube's monsterstate: M_SLEEP, M_SEARCH, M_HOME, M_AIMING, M_ATTACKING, M_PAIN */
	int trigger;            /* time (ms, game clock) at which the current state is re-evaluated */
	int targetyaw, move, enemy, anger;      /* enemy: -1 = the player, else a monster index (infighting) */
	int atx, aty, atz;      /* where it is aiming: the enemy's position when it decided to fire */
	int vz, onfloor, blocked, jumpnext;
	int painflash;
	int radius, eye, scale;
	int alive;
	int anim, animt0, moving, deadvar;   /* animation: see ANIM_*; animt0 = game tick it started */
	/* quickplay bots (bot != 0): a monster that plays like a player (bots.inc) */
	int bot, team, frags, gun, gunwait, myaw, respawn, think, role, stuckt, sx, sy, goal, lockyaw, lockt;
	int aimt, aware;                  /* (not saved) bots: when the enemy came into sight; monsters: how long they have seen you */
	int caps, deaths;                 /* (not saved) bots: flags brought home, times fragged */
	int16_t ammo[NUMGUNS];
} Monster;

enum { ANIM_IDLE, ANIM_WALK, ANIM_ATTACK, ANIM_PAIN, ANIM_DEATH };
extern int game_ticks;                      /* 60 Hz game clock, drives animation */

#define MAXMON 96
#define MAXITEM 128
#define MAXPROJ 24
/* a projectile flies in a straight line from s to s + d over `steps` steps (3 a tick), so slow shots aimed almost level
   do not lose their slope to rounding; owner: -1 = player, else monster index; life != 0 while in flight */
typedef struct { int x, y, z, sx, sy, sz, dx, dy, dz, step, steps, gun, owner, life; } Proj;
enum { M_SLEEP, M_SEARCH, M_HOME, M_AIMING, M_ATTACKING, M_PAIN };       /* monster states (Cube monster.cpp) */
typedef struct { int x, y, z, type, taken, respawn; } Item;

extern Player pl;
extern Monster mons[MAXMON];  extern int nmons;
extern Item items[MAXITEM];   extern int nitems;
extern Proj projs[MAXPROJ];
typedef struct { int x, y, z, life; } Fx;
#define MAXFX 12
extern Fx fxs[MAXFX];
extern int game_msg_timer; extern const char *game_msg;
/* carrots (triggers), teleports and jump pads: Cube's entities.cpp.  x,y = the entity's map point (FX), z = floor under it */
#define MAXTRIG 48
typedef struct { int x, y, z, attr1; uint8_t type, attr2, attr3, attr4; int spawned; } Trig;
extern Trig trigs[MAXTRIG]; extern int ntrigs;
extern int level_done, level_secs;                      /* 0 = playing, 1 = reached the exit, 2 = cleared the map (Cube's endsp) */
extern int total_monsters;

void game_start(int mapindex);
/* ---- quickplay (bots.inc): deathmatch, team deathmatch, capture the flag against bots ---- */
enum { GM_SP, GM_DM, GM_TDM, GM_CTF, GM_KOTH };
extern int game_mode, team_score[2], pl_team, pl_frags, pl_caps, match_over, match_limit;
void game_quick_begin(void);
int  koth_hill(int *x, int *y, int *z, int *owner);   /* the hill's middle (FX) and who holds it (-1 nobody, 0 red, 1 blue, 2 contested); 0 = no hill */
extern int qp_waiting, pl_flying;                      /* quickplay warm-up: the map is yours alone until you press fire */
typedef struct { int hx, hy, hz, x, y, z, state, carrier, timer; } Flag;    /* state: 0 home, 1 carried, 2 dropped */
extern Flag flags[2];
extern int nflags;
void game_start_quick(int mapindex, int mode, int nbots);
int  qp_leader_frags(void);                  /* the best bot's frags (deathmatch) */
extern int difficulty;                       /* 0 easy, 1 normal, 2 hard */
extern int demo_mode, demo_done;             /* attract-mode demo (game.c, route from tools/mkdemo.py) */
void demo_begin(void);
int  game_save_size(void);                   /* save games: the whole state as plain data */
void game_save(void *buf);
int  game_load(const void *buf);             /* 0 = not a valid save */
int  game_current_map(void);
int  game_elapsed_secs(void);
void game_tick(int buttons_held, int buttons_pressed, int lx, int ly, int rx, int ry);
const char *gun_name(int g);
/* ---- link play (game.c linkplay.inc, link.c): two consoles on the link cable, each running the same game, tick by
   tick, from both pads.  Human 0 (the host's) lives in `pl`; human 1 is in a store and is shown to everything else
   by a stand-in bot ("the proxy").  Swapping the two exchanges which human `pl` is, so all the player code serves both. ---- */
extern int link_on;                          /* a link game is on */
extern int link_me;                          /* which human this console is: 0 = host (P1), 1 = guest (P2) */
extern int link_phase;                       /* which human is in `pl` right now */
extern int game_msg_for;                     /* game_msg is for: -1 everyone, else that human only */
typedef struct { uint16_t held, pressed; int8_t lx, ly, rx, ry; uint8_t quit; } LinkPad;   /* quit: leaving (the other wins) */
typedef struct { uint8_t map, mode, bots, teams, skill, move, turn, pad; uint8_t look[2], invert[2], aim[2]; uint32_t seed; } LinkSetup;
enum { LT_VERSUS, LT_TOGETHER };             /* LinkSetup.teams: P1 red v P2 blue, or both red */
void game_start_link(const LinkSetup *s, int me);
void game_tick_link(const LinkPad *p0, const LinkPad *p1);
void game_link_view(int on);                 /* on the guest: bring human 1 into `pl` to draw the screen (and back) */
void game_link_end(void);                    /* back to a one-console game (restores the options the host set) */
void game_link_abort(const char *why);       /* the cable failed: stop the match with a message */
uint32_t game_link_hash(void);               /* the state both consoles must agree on */
int  game_link_ready(int h);                 /* warm-up: has human h pressed fire */
int  game_link_frags(int h);
/* the match menu's table: player i (the humans first, then the bots); 0 when there is no player i */
typedef struct { const char *name; int frags, deaths, caps, team, you; } PlayerStat;
int  game_player_stat(int i, PlayerStat *s);
const Player *game_listener(void);           /* whose ears: this console's human, wherever it is stored */
int  game_sound_remote(void);                /* the human in `pl` is the other console's: its own sounds are positioned */
/* link.c: the cable */
enum { LS_IDLE, LS_ASK, LS_LOAD, LS_SYNC, LS_PLAY };   /* nothing on; offered a game; agreed (load it); loaded; playing */
void link_open(void);                        /* start the serial port (once, from the link screen) */
void link_poll(void);                        /* every frame */
void link_listen(void);                      /* the link screen is up: say hello to the other console */
int  link_peer(void);                        /* another console has been heard lately */
int  link_state(void);
const char *link_error(void);                /* why the last game or offer ended, or 0 */
void link_host(const LinkSetup *s);          /* offer a game */
int  link_offer(LinkSetup *s);               /* the other console offered one: take it (1) */
int  link_agreed(LinkSetup *s, int *me);     /* a game both agreed to: load it, then link_loaded() */
void link_loaded(void);
void link_stop(void);
int  link_frame(int held, int pressed, int lx, int ly, int rx, int ry, int ticks);   /* play: returns ticks run */
int  link_waiting(void);
void link_quit(void);                        /* leave the match: the other player wins (sent with the next pad) */                     /* playing, but held up for the other console's pad */
int  gun_delay(int g);                       /* ticks between shots */
int  mon_color(int type, int *r, int *g, int *b);

/* ---- menu.c / save.c ---- */
void menu_init(void);
void menu_tick(int held, int pressed, int lx, int ly, int rx, int ry);
void menu_draw(void);
int  menu_world_visible(void);
int  menu_hud_visible(void);
/* who you are: 0 Giga Chad, 1 Giga Stacy - the status bar portrait (render.c) and the voice (sound.c) */
extern int opt_char;
void render_set_face(int who);               /* loads WHO0.BIN / WHO1.BIN (named to sort after SYSTEM.CNF) into the portrait's VRAM */
void render_face_preview(int x, int y);      /* the portrait at twice the size (menus) */
void snd_set_voice(int who);                 /* loads VOICE.PAK / VOICE_F.PAK over the voice lines */
int  menu_paused(void);                      /* a menu is up over the game (a link game goes on: the pad is not the player's) */
extern int opt_sfx, opt_music, opt_speech;  /* volumes 0..10 */
extern int opt_move, opt_turn;              /* 1..10: 1 = PS1 tuning, 10 = Cube's own speed (game.c) */
extern int opt_look, opt_invert, opt_aim;    /* stick look speed 1..10, invert up/down, aim assist on/off */
/* ---- campaigns (save.c): Cube's single-player map chains, in Cube's order (level_trigger "map next" scripts).  world =
   index into map_names, or -1 for a map not ported yet (too big for now); the unlock chain steps over those. ---- */
typedef struct { const char *name, *title; int8_t world; uint8_t episode; } CatMap;
typedef struct { const char *name, *author; uint8_t first, count; } Episode;
#define CAT_N 23
#define EP_N 4
extern const CatMap catalog[CAT_N];
extern const Episode episodes[EP_N];

/* ---- profiles on the memory card (save.c): one per slot ---- */
enum { MR_OPEN = 1, MR_BEATEN = 2, MR_ALLKILLS = 4 };
typedef struct { uint8_t flags, best_kills; uint16_t best_secs; } MapRec;
typedef struct {
	char magic[4]; int version;
	char name[12]; int diff;
	int opt_move, opt_turn, opt_sfx, opt_music;
	MapRec maps[32];
	int has_state, state_cat;                    /* a game saved from the pause menu, on catalog map state_cat */
	int opt_look, opt_invert, opt_aim, opt_speech;   /* version 2 */
	int character;                               /* version 3: 0 Giga Chad, 1 Giga Stacy */
} Profile;
extern Profile prof;
extern int prof_slot;                        /* card slot of the loaded profile, -1 = none */
extern int prof_card_ok;                     /* 0: the last card write failed (no card) - the profile lives in memory only */
int  prof_new(int slot, const char *name, int diff);     /* first map of each episode open; writes it to the card (0 = could not) */
int  prof_load(int slot);                    /* 1 = ok */
int  prof_save(void);                        /* writes the profile (keeps a saved game in it) */
int  prof_save_game(int cat);                /* the game in progress, into the profile */
int  prof_resume(void);                      /* load the saved game; 1 = ok */
int  prof_describe(int slot, char *out);     /* one line for the slot lists; 0 = empty */
int  prof_complete(int cat, int allkills, int secs, int kills);   /* record a finished map, open the next; returns the
                                                                      catalog index opened, or -1 */
int  cat_of_world(int world);
extern uint8_t ui_icon[160];

/* ---- sound.c: Cube's sounds, numbered as in Cube (data/sounds.cfg) ---- */
enum {
	S_JUMP = 0, S_LAND, S_RIFLE, S_PUNCH1, S_SG, S_CG, S_RLFIRE, S_RLHIT, S_WEAPLOAD, S_ITEMAMMO, S_ITEMHEALTH,
	S_ITEMARMOUR, S_ITEMPUP, S_ITEMSPAWN, S_TELEPORT, S_NOAMMO, S_PUPOUT, S_PAIN1, S_PAIN2, S_PAIN3, S_PAIN4, S_PAIN5, S_PAIN6,
	S_DIE1, S_DIE2, S_FLAUNCH, S_FEXPLODE, S_SPLASH1, S_SPLASH2, S_GRUNT1, S_GRUNT2, S_RUMBLE, S_PAINO, S_PAINR, S_DEATHR,
	S_PAINE, S_DEATHE, S_PAINS, S_DEATHS, S_PAINB, S_DEATHB, S_PAINP, S_PIGGR2, S_PAINH, S_DEATHH, S_PAIND, S_DEATHD,
	S_PIGR1, S_ICEBALL, S_SLIMEBALL, S_JUMPPAD,
	/* voice lines (assets/voice): the dude, then the announcer */
	V_GRUNT1, V_GRUNT2, V_GRUNT3, V_DIE, V_OHYEAH, V_EASY, V_HURT, V_NEXT, V_STAYDOWN, V_TOOSLOW, V_LOCKED, V_SWITCH, V_COMEON,
	V_CLEANSWEEP, V_WHOSNEXT, V_CAKE, V_ROCK, V_HOLYSHIT
};
int  snd_say_any(int id, int interrupt);     /* the same, even for the other console's human (match news) */
int  snd_say(int id, int interrupt);         /* a voice line on its own channel; 0 if one is already playing and !interrupt */
int  snd_speaking(void);                     /* the voice line playing, or -1 */
void sound_init(void);
void sound_update(void);                     /* once a frame */
int  snd_duck(void);                          /* 0..256: how far speech has turned everything else down */
/* ---- music.c: CD-XA, a track per map ---- */
void music_init(void);
void music_update(int duck);                  /* once a frame */
void music_play(int track);                   /* 0 = the menus' track, -1 = none */
void music_for_map(int cat);                  /* the track of catalog map cat */
void music_cd_taken(void);                    /* cdload.c, before it reads */
void sound_stop_all(void);
void snd_play(int id);                       /* the player's own sound: full volume, centred */
void snd_play_at(int id, int x, int y, int z);   /* a sound somewhere in the world (FX) */
int  yaw_to(int dx, int dy);                 /* game.c: yaw that faces (dx, dy) */

/* ---- render.c ---- */
/* menu graphics: Cube's font (size = height in pixels; colour r | g << 8 | b << 16, 0x808080 = as drawn), the logo,
 * solid and darkening rectangles.  Drawn on top of everything, in call order. */
int  ui_text(int x, int y, const char *s, int size, uint32_t col);
int  ui_text_width(const char *s, int size);
void ui_logo(int cx, int cy, int size, int angle, uint32_t col);
void ui_rect(int x0, int y0, int x1, int y1, uint32_t col);
void ui_dim(int x0, int y0, int x1, int y1, int amount);       /* subtract amount (0..255) from what is there */
void render_init(void);
void render_loading(const char *what);   /* shows a LOADING screen */
void render_frame(void);
int  render_view_cells(void);      /* the view distance right now, in cells (fog is black there) */

int isqrt(int v);
int vlen3(int dx, int dy, int dz);    /* overflow-safe vector lengths (world.c) */
int vlen2(int dx, int dy);
int iabs(int v);

#define NUM_MAPS 27
extern const char *map_names[NUM_MAPS];
#endif
