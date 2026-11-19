/* Cube PSX profiles on the memory card (slot 1), and the campaign catalog.
 *
 * Each of the 3 slots is its own memory card file "BASCUS-99999CUBEn", 2 blocks (16 KB): the standard 256-byte header
 * the BIOS file browser shows (title in Shift-JIS, the Cube icon), then the Profile (name, difficulty, options, progress
 * on every map), then - if the player saved from the pause menu - the game state from game_save().  The host build
 * (CUBE_HOST) keeps the files in the working directory instead. */
#include "cube.h"
#include <string.h>
#include <stdio.h>
#ifndef CUBE_HOST
#include <psxapi.h>
#include <sys/fcntl.h>
#endif

#define SAVE_BLOCKS 2
#define SAVE_BYTES (SAVE_BLOCKS * 8192)
static uint8_t savebuf[SAVE_BYTES] __attribute__((aligned(4)));
uint8_t ui_icon[160];                        /* 16x16 4bpp icon + CLUT, filled from UI.PAK by render.c */

#define PROF_OFS 0x100
#define STATE_OFS 0x300
_Static_assert(sizeof(Profile) <= STATE_OFS - PROF_OFS, "profile overlaps the saved game");

/* Cube's campaigns in Cube's order; the titles are the maps' own */
const CatMap catalog[CAT_N] = {
	{ "pig01", "Surface Tension", 0, 0 },      { "pig02", "No Turning Back", 7, 0 },  { "pig03", "Enclosure", 8, 0 },
	{ "pig04", "Bright Darkness", 9, 0 },     { "pig05", "Untitled", 10, 0 },          { "pig06", "Untitled", 14, 0 },
	{ "pig07", "Untitled", 5, 0 },             { "pig08", "Gauntlet of the Damned", 11, 0 },
	{ "pig09", "The Dark Hand Beckons", 2, 0 }, { "pigfin", "My Final Breath", 6, 0 },
	{ "cruel01", "Cruel Part I", 1, 1 },       { "cruel02", "Cruel Part II", 4, 1 },
	{ "wsg1", "Why So Green?", 3, 2 },          { "wsg2", "Why Still Green?", 12, 2 },  { "wsg3", "Why So Greedy", 13, 2 },
	{ "xeno01", "Intake", 16, 3 }, { "xeno02", "Flooded Works", 17, 3 }, { "xeno03", "Night Courtyard", 18, 3 }, { "xeno04", "The Vaults", 19, 3 },
	{ "xeno05", "Cisterns", 20, 3 }, { "xeno06", "Gauntlet", 21, 3 }, { "xeno07", "Starfall Keep", 22, 3 }, { "xeno08", "Xenocide", 23, 3 },
};
const Episode episodes[EP_N] = {
	{ "PIGCAM", "Jonathan Whiting", 0, 10 }, { "CRUEL", "unicious", 10, 2 }, { "WHY SO GREEN", "SirLiveAlot", 12, 3 }, { "XENO", "Xenocide21", 15, 8 },
};
int cat_of_world(int world) { int i; for (i = 0; i < CAT_N; i++) if (catalog[i].world == world) return i; return -1; }

Profile prof;
int prof_slot = -1;
int prof_card_ok = 1;                        /* did the last write to the card work?  0 = no card: play on, nothing kept */

static void name_of(int slot, char *out) {
#ifdef CUBE_HOST
	sprintf(out, "cubepsx_save%d.mcd", slot);
#else
	sprintf(out, "bu00:BASCUS-99999CUBE%d", slot);
#endif
}

/* ASCII -> full-width Shift-JIS, which is what the BIOS memory card screen shows */
static void sjis(uint8_t *dst, const char *s, int max) {
	int n = 0;
	memset(dst, 0, max);
	for (; *s && n + 2 <= max; s++) {
		unsigned c = (unsigned char)*s, code = 0x8140;
		if (c >= 'A' && c <= 'Z') code = 0x8260 + (c - 'A');
		else if (c >= 'a' && c <= 'z') code = 0x8281 + (c - 'a');
		else if (c >= '0' && c <= '9') code = 0x824f + (c - '0');
		else if (c == '-') code = 0x817c;
		else if (c == '/') code = 0x815e;
		else if (c == ':') code = 0x8146;
		else if (c == '.') code = 0x8144;
		dst[n++] = code >> 8; dst[n++] = code & 255;
	}
}

static int card_write(int slot, const void *buf, int len) {
	char name[32]; int fd, n;
	name_of(slot, name);
#ifdef CUBE_HOST
	{ FILE *f = fopen(name, "wb"); if (!f) return 0; n = (int)fwrite(buf, 1, len, f); fclose(f); fd = 0; (void)fd; return n == len; }
#else
	fd = open(name, FWRITE);
	if (fd < 0) {                                      /* not there yet: create it with its size in blocks */
		fd = open(name, FCREATE | (SAVE_BLOCKS << 16));
		if (fd < 0) return 0;
		close(fd);
		fd = open(name, FWRITE);
		if (fd < 0) return 0;
	}
	n = write(fd, buf, len);
	close(fd);
	return n == len;
#endif
}

static int card_read(int slot, void *buf, int len) {      /* len: a multiple of 128 */
	char name[32]; int fd, n;
	name_of(slot, name);
#ifdef CUBE_HOST
	{ FILE *f = fopen(name, "rb"); if (!f) return 0; n = (int)fread(buf, 1, len, f); fclose(f); fd = 0; (void)fd; return n == len; }
#else
	fd = open(name, FREAD);
	if (fd < 0) return 0;
	n = read(fd, buf, len);
	close(fd);
	return n == len;
#endif
}

static void header(void) {
	char title[40];
	savebuf[0] = 'S'; savebuf[1] = 'C'; savebuf[2] = 0x11; savebuf[3] = SAVE_BLOCKS;
	sprintf(title, "CUBE PSX %s", prof.name);
	sjis(savebuf + 4, title, 64);
	memcpy(savebuf + 0x60, ui_icon + 128, 32);              /* CLUT */
	memcpy(savebuf + 0x80, ui_icon, 128);                   /* one 16x16 frame */
}

/* the first ported map of an episode at or after catalog index c (-1 if none) */
static int ported_from(int c, int ep) {
	for (; c < episodes[ep].first + episodes[ep].count; c++) if (catalog[c].world >= 0) return c;
	return -1;
}

int prof_new(int slot, const char *name, int diff) {
	int e;
	memset(&prof, 0, sizeof(prof));
	memcpy(prof.magic, "CPPF", 4); prof.version = 3;
	strncpy(prof.name, name, sizeof(prof.name) - 1); prof.diff = diff;
	prof.opt_move = opt_move; prof.opt_turn = opt_turn; prof.opt_sfx = opt_sfx; prof.opt_music = opt_music;
	prof.opt_look = opt_look; prof.opt_invert = opt_invert; prof.opt_aim = opt_aim; prof.opt_speech = opt_speech;
	prof.character = opt_char;
	for (e = 0; e < EP_N; e++) { int c = ported_from(episodes[e].first, e); if (c >= 0) prof.maps[c].flags |= MR_OPEN; }
	prof_slot = slot;
	memset(savebuf, 0, sizeof(savebuf));
	return prof_save();
}

int prof_save(void) {
	if (prof_slot < 0) return 0;
	if (prof_slot >= 3) return 1;                        /* (no card: kept in memory only) */
	prof.opt_move = opt_move; prof.opt_turn = opt_turn; prof.opt_sfx = opt_sfx; prof.opt_music = opt_music;
	prof.opt_look = opt_look; prof.opt_invert = opt_invert; prof.opt_aim = opt_aim; prof.opt_speech = opt_speech;
	prof.character = opt_char; prof.version = 3;
	header();
	memcpy(savebuf + PROF_OFS, &prof, sizeof(prof));
	return prof_card_ok = card_write(prof_slot, savebuf, SAVE_BYTES);    /* tried every time: a card put in later works */
}

int prof_save_game(int cat) {
	if (prof_slot < 0 || game_save_size() > SAVE_BYTES - STATE_OFS) return 0;
	game_save(savebuf + STATE_OFS);
	prof.has_state = 1; prof.state_cat = cat;
	return prof_save();
}

int prof_load(int slot) {
	Profile p;
	if (!card_read(slot, savebuf, SAVE_BYTES)) return 0;
	memcpy(&p, savebuf + PROF_OFS, sizeof(p));
	if (memcmp(p.magic, "CPPF", 4) || p.version < 1 || p.version > 3) return 0;
	if (p.version == 1) { p.opt_look = 1; p.opt_invert = 0; p.opt_aim = 1; p.opt_speech = p.opt_sfx; }     /* a v0.10 profile */
	if (p.version < 3) p.character = 0;                                                                         /* before Giga Stacy */
	prof = p; prof_slot = slot;
	opt_move = prof.opt_move; opt_turn = prof.opt_turn; opt_sfx = prof.opt_sfx; opt_music = prof.opt_music;
	opt_look = prof.opt_look; opt_invert = prof.opt_invert; opt_aim = prof.opt_aim; opt_speech = prof.opt_speech;
	opt_char = prof.character & 1;
	difficulty = prof.diff;
	return 1;
}

int prof_resume(void) {
	if (prof_slot < 0 || !prof.has_state) return 0;
	difficulty = prof.diff;
	return game_load(savebuf + STATE_OFS);
}

int prof_describe(int slot, char *out) {
	static const char *dn[3] = { "EASY", "NORMAL", "HARD" };
	Profile p; int i, beaten = 0, ported = 0;
	if (!card_read(slot, savebuf, 0x300)) { sprintf(out, "%d  EMPTY", slot + 1); return 0; }
	memcpy(&p, savebuf + PROF_OFS, sizeof(p));
	if (memcmp(p.magic, "CPPF", 4) || p.version < 1 || p.version > 3) { sprintf(out, "%d  EMPTY", slot + 1); return 0; }
	for (i = 0; i < CAT_N; i++) if (catalog[i].world >= 0) { ported++; if (p.maps[i].flags & MR_BEATEN) beaten++; }
	sprintf(out, "%d  %-10s %-6s %d/%d", slot + 1, p.name, dn[p.diff % 3], beaten, ported);
	if (prof_slot == slot) card_read(slot, savebuf, SAVE_BYTES);    /* savebuf mirrors the loaded profile's file */
	return 1;
}

int prof_complete(int cat, int allkills, int secs, int kills) {
	MapRec *r; int nx = -1;
	if (cat < 0 || cat >= CAT_N) return -1;
	r = &prof.maps[cat];
	if (!(r->flags & MR_BEATEN) || secs < r->best_secs) r->best_secs = secs > 65535 ? 65535 : secs;
	if (kills > r->best_kills) r->best_kills = kills > 255 ? 255 : kills;
	r->flags |= MR_OPEN | MR_BEATEN | (allkills ? MR_ALLKILLS : 0);
	nx = ported_from(cat + 1, catalog[cat].episode);
	if (nx >= 0) prof.maps[nx].flags |= MR_OPEN;
	if (prof.has_state && prof.state_cat == cat) prof.has_state = 0;      /* that saved game is done with */
	prof_save();
	return nx;
}
