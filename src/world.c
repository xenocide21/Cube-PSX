#include "cube.h"
#include <string.h>

const char *map_names[NUM_MAPS] = { "pig01", "cruel01", "pig09", "wsg1", "cruel02", "pig07", "pigfin",
	"pig02", "pig03", "pig04", "pig05", "pig08", "wsg2", "wsg3", "pig06", "fcubes",
	"xeno01", "xeno02", "xeno03", "xeno04", "xeno05", "xeno06", "xeno07", "xeno08", "xdm1", "xdm2", "xdm3" };      /* (16-26: the XENO pack by Xenocide21) */
static const char *map_files[NUM_MAPS][2] = { { "PIG01.CMP", "PIG01.TEX" }, { "CRUEL01.CMP", "CRUEL01.TEX" }, { "PIG09.CMP", "PIG09.TEX" }, { "WSG1.CMP", "WSG1.TEX" },
	{ "CRUEL02.CMP", "CRUEL02.TEX" }, { "PIG07.CMP", "PIG07.TEX" }, { "PIGFIN.CMP", "PIGFIN.TEX" },
	{ "PIG02.CMP", "PIG02.TEX" }, { "PIG03.CMP", "PIG03.TEX" }, { "PIG04.CMP", "PIG04.TEX" }, { "PIG05.CMP", "PIG05.TEX" }, { "PIG08.CMP", "PIG08.TEX" }, { "WSG2.CMP", "WSG2.TEX" }, { "WSG3.CMP", "WSG3.TEX" }, { "PIG06.CMP", "PIG06.TEX" }, { "FCUBES.CMP", "FCUBES.TEX" },
	{ "XENO01.CMP", "XENO01.TEX" }, { "XENO02.CMP", "XENO02.TEX" }, { "XENO03.CMP", "XENO03.TEX" }, { "XENO04.CMP", "XENO04.TEX" }, { "XENO05.CMP", "XENO05.TEX" }, { "XENO06.CMP", "XENO06.TEX" }, { "XENO07.CMP", "XENO07.TEX" }, { "XENO08.CMP", "XENO08.TEX" }, { "XDM1.CMP", "XDM1.TEX" }, { "XDM2.CMP", "XDM2.TEX" }, { "XDM3.CMP", "XDM3.TEX" } };
uint32_t g_texbuf[TEXBUF_SZ / 4];             /* the map, after its textures went to video memory (cube.h) */
void (*world_tex_cb)(void);                  /* render.c: upload the texture pack now, before the map is loaded over it */
void (*world_loading_cb)(const char *what);
static int generation;
static const uint8_t *hdr;
static int ssize, nents;
#define ssize_g ssize
#define LIGHTERR 64                 /* Cube's default is 24; a little looser merges more of the distant squares */
static Cell *cells;
static const MapEnt *ents;
static const Cell solid_cell = { .type = SOLID_T, .floor = 0, .ceil = 16, .wtex = 2, .ftex = 3, .ctex = 4, .utex = 2, .tl = 0x0a0a0a00 };

/* ---- Cube's mip hierarchy (world.cpp remip) ---- */
#define MAXS 256
Sqr *wmip[MAXMIPS + 1];
int world_sfactor;
static const Sqr solid_sqr = { .type = SOLID_T, .floor = 0, .ceil = 16, .wtex = 2, .ftex = 3, .ctex = 4, .utex = 2, .tl = 0x0a0a0a00 };
#define LR(s) ((int)(s)->r)
#define LG(s) ((int)(s)->g)
#define LB(s) ((int)(s)->b)

static const Sqr *sq(int level, int x, int y) {
	int sz = ssize_g >> level;
	if (x < 0 || y < 0 || x >= sz || y >= sz) return &solid_sqr;
	return &wmip[level][y * sz + x];
}

static void remip_level(int level, int x0, int y0, int x1, int y1) {     /* rebuild level+1 squares covering [x0,x1) x [y0,y1) of `level` */
	int ws = ssize_g >> level, vs = ws >> 1, x, y, i;
	Sqr *w = wmip[level], *v = wmip[level + 1];
	x0 &= ~1; y0 &= ~1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > ws) x1 = ws;
	if (y1 > ws) y1 = ws;
	for (y = y0; y < y1; y += 2) for (x = x0; x < x1; x += 2) {
		Sqr *o[4], *r = &v[(y >> 1) * vs + (x >> 1)]; int nums[6] = { 0, 0, 0, 0, 0, 0 };
		o[0] = &w[y * ws + x]; o[1] = o[0] + 1; o[2] = o[0] + ws + 1; o[3] = o[0] + ws;
		*r = *o[0];
		for (i = 0; i < 4; i++) nums[o[i]->type]++;
		r->type = SEMISOLID_T;
		for (i = 0; i < 6; i++) if (nums[i] == 4) r->type = i;
		if (r->type != SOLID_T) {
			int fl = 127, ce = -128;
			for (i = 0; i < 4; i++) if (o[i]->type != SOLID_T) {
				int fh = o[i]->floor, ch = o[i]->ceil;
				if (r->type == SEMISOLID_T) {
					if (o[i]->type == FHF_T) fh -= o[i]->vdelta / 4 + 2;
					if (o[i]->type == CHF_T) ch += o[i]->vdelta / 4 + 2;
				}
				if (fh < fl) fl = fh;
				if (ch > ce) ce = ch;
			}
			r->floor = fl < -128 ? -128 : fl; r->ceil = ce > 127 ? 127 : ce;
		}
		if (r->type == CORNER_T) { r->defer = 0; continue; }
		r->defer = 1;
		if (r->type == SOLID_T) {
			if (o[0]->wtex != o[3]->wtex || o[1]->wtex != o[3]->wtex || o[2]->wtex != o[3]->wtex) continue;
		} else {
			int bad = 0;
			for (i = 0; i < 3 && !bad; i++) {
				if (o[i]->type != o[3]->type || o[i]->floor != o[3]->floor || o[i]->ceil != o[3]->ceil
				 || o[i]->ftex != o[3]->ftex || o[i]->ctex != o[3]->ctex || o[i]->utex != o[3]->utex || o[i]->wtex != o[3]->wtex
				 || iabs(LR(o[i + 1]) - LR(o[0])) > LIGHTERR || iabs(LG(o[i + 1]) - LG(o[0])) > LIGHTERR || iabs(LB(o[i + 1]) - LB(o[0])) > LIGHTERR) bad = 1;
			}
			if (bad) continue;
			if (r->type == CHF_T || r->type == FHF_T) {       /* a height field is a perfect mip only if its slopes lie on one line */
				if (o[0]->vdelta - o[1]->vdelta != o[1]->vdelta - sq(level, x + 2, y)->vdelta
				 || o[0]->vdelta - o[2]->vdelta != o[2]->vdelta - sq(level, x + 2, y + 2)->vdelta
				 || o[0]->vdelta - o[3]->vdelta != o[3]->vdelta - sq(level, x, y + 2)->vdelta
				 || o[3]->vdelta - o[2]->vdelta != o[2]->vdelta - sq(level, x + 2, y + 1)->vdelta
				 || o[1]->vdelta - o[2]->vdelta != o[2]->vdelta - sq(level, x + 1, y + 2)->vdelta) continue;
			}
		}
		if (level > 0 && (o[0]->defer || o[1]->defer || o[2]->defer || o[3]->defer)) continue;      /* (level 0 holds tags there) */
		r->defer = 0;
	}
}

/* The highest point of anything drawn in each square, at every mip level (units; 127 = no limit): the renderer skips
   whatever stays under the horizon of nearer raised ground (Facing Cubes is all floors, no solid walls to hide behind).
   A cell: its floor, its neighbours' floors (the walls up to them), its ceiling if one is drawn or a wall hangs from it,
   the water; a solid cell: the open neighbours' ceilings its walls reach to. */
int8_t *world_htop[MAXMIPS + 1];
static void htop_cells(int x0, int y0, int x1, int y1) {
	int x, y, k, w = world_water();
	static const int8_t nx[4] = { 1, -1, 0, 0 }, ny[4] = { 0, 0, 1, -1 };
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > ssize_g) x1 = ssize_g;
	if (y1 > ssize_g) y1 = ssize_g;
	for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) {
		const Sqr *c = sq(0, x, y); int t;
		if (c->type == SOLID_T || c->type == CORNER_T) {
			t = -128;
			for (k = 0; k < 4; k++) { const Sqr *n = sq(0, x + nx[k], y + ny[k]); if (n->type != SOLID_T && n->ceil > t) t = n->ceil + (n->type == CHF_T ? 66 : 0); }
			if (c->type == CORNER_T) t = 127;
		} else {
			t = c->floor;
			if (c->ctex) t = c->ceil + (c->type == CHF_T ? 66 : 0);                 /* a ceiling is drawn: everything up to it */
			for (k = 0; k < 4; k++) {
				const Sqr *n = sq(0, x + nx[k], y + ny[k]);
				if (n->type == SOLID_T || n->ceil < c->ceil) { if (c->ceil > t) t = c->ceil; }
				else if (n->floor > t) t = n->floor;
			}
			if (w != NO_WATER && w > t) t = w;
		}
		world_htop[0][y * ssize_g + x] = t > 127 ? 127 : t < -128 ? -128 : t;
	}
}
static void htop_up(int level, int x0, int y0, int x1, int y1) {          /* level+1 from level, over level's [x0,x1) x [y0,y1) */
	int ws = ssize_g >> level, vs = ws >> 1, x, y;
	const int8_t *a = world_htop[level]; int8_t *b = world_htop[level + 1];
	x0 &= ~1; y0 &= ~1;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > ws) x1 = ws;
	if (y1 > ws) y1 = ws;
	for (y = y0; y < y1; y += 2) for (x = x0; x < x1; x += 2) {
		int t = a[y * ws + x], u = a[y * ws + x + 1], v = a[(y + 1) * ws + x], q = a[(y + 1) * ws + x + 1];
		if (u > t) t = u;
		if (v > t) t = v;
		if (q > t) t = q;
		b[(y >> 1) * vs + (x >> 1)] = t;
	}
}

void world_remip(int x, int y, int xs, int ys) {
	int level, x0 = x - 1, y0 = y - 1, x1 = x + xs + 1, y1 = y + ys + 1;
	if (world_htop[0]) {
		int a0 = x0 - 1, b0 = y0 - 1, a1 = x1 + 2, b1 = y1 + 2;
		htop_cells(a0, b0, a1, b1);
		for (level = 0; level < world_sfactor; level++) { htop_up(level, a0, b0, a1 + 1, b1 + 1); a0 = (a0 >> 1) - 1; b0 = (b0 >> 1) - 1; a1 = (a1 >> 1) + 2; b1 = (b1 >> 1) + 2; }
	}
	for (level = 0; level < world_sfactor; level++) {
		remip_level(level, x0, y0, x1 + 1, y1 + 1);
		x0 = (x0 >> 1) - 1; y0 = (y0 >> 1) - 1; x1 = (x1 >> 1) + 2; y1 = (y1 >> 1) + 2;
	}
}

/* level 0 is the map's own cells; the mip levels follow them in the same memory */
static uint8_t *spare, *spare_end;               /* the map area left over after the map (a 128 map uses a quarter) */
uint8_t *world_spare(int *size) { *size = spare ? (int)(spare_end - spare) : 0; return spare; }
static int build_sqrs(uint8_t *after, uint8_t *end) {
	int level, i, n = ssize_g * ssize_g; Sqr *p;
	p = (Sqr *)(((uintptr_t)after + 3) & ~3);
	wmip[0] = cells;
	for (level = 1; level <= world_sfactor; level++) { wmip[level] = p; p += (ssize_g >> level) * (ssize_g >> level); }
	if ((uint8_t *)p > end) return 0;
	spare = (uint8_t *)(((uintptr_t)p + 3) & ~(uintptr_t)3); spare_end = end;
	for (i = 0; i < n; i++) if (cells[i].type > SPACE_T) cells[i].type = SPACE_T;
	for (level = 0; level < world_sfactor; level++) remip_level(level, 0, 0, ssize_g >> level, ssize_g >> level);
	{   /* the square heights, if they fit in the spare (a 128 map: 22 KB of its 800; a 256 map has no room - and is all rooms) */
		int8_t *h = (int8_t *)spare; int need = 0;
		for (level = 0; level <= world_sfactor; level++) need += (ssize_g >> level) * (ssize_g >> level);
		for (level = 0; level <= MAXMIPS; level++) world_htop[level] = 0;
		if (h + need + 4 <= (int8_t *)spare_end) {
			for (level = 0; level <= world_sfactor; level++) { world_htop[level] = h; h += (ssize_g >> level) * (ssize_g >> level); }
			spare = (uint8_t *)(((uintptr_t)h + 3) & ~(uintptr_t)3);
			htop_cells(0, 0, ssize_g, ssize_g);
			for (level = 0; level < world_sfactor; level++) htop_up(level, 0, 0, ssize_g >> level, ssize_g >> level);
		}
	}
	return 1;
}

#define BODYH (870)   /* eyeheight 3.2 + aboveeye 0.2 (FX) */

int iabs(int v) { return v < 0 ? -v : v; }

/* lengths of map-space vectors (1/256 cell units).  Across a 256 map a squared length passes 2^31, so long vectors are
   measured at 1/16 precision instead (still far finer than anything that uses them) */
int vlen3(int dx, int dy, int dz) {
	int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, az = dz < 0 ? -dz : dz;
	if ((ax | ay | az) < 16384) return isqrt(dx * dx + dy * dy + dz * dz);
	dx >>= 4; dy >>= 4; dz >>= 4;
	return isqrt(dx * dx + dy * dy + dz * dz) << 4;
}
int vlen2(int dx, int dy) { return vlen3(dx, dy, 0); }

uint8_t world_seen[2048];
int seen_gen;

int isqrt(int v) {
	unsigned int x = v, r = 0, b = 1u << 30;
	if (v <= 0) return 0;
	while (b > x) b >>= 2;
	while (b) {
		if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
		b >>= 2;
	}
	return (int)r;
}

static int loaded_idx = -1;
void world_forget(void) { loaded_idx = -1; }    /* something else was loaded through g_texbuf: the map is gone */
static int loading_idx = -1;
static void build_mapmodels(void);
int world_load(int idx) {
	if (idx < 0 || idx >= NUM_MAPS) return 0;
	if (idx == loaded_idx) return 1;          /* already in memory: only door cells ever change, and game_start resets those */
	loaded_idx = -1; loading_idx = idx;
	if (world_loading_cb) world_loading_cb(map_names[idx]);
	generation++; memset(world_seen, 0, sizeof(world_seen)); seen_gen++;
	/* textures first: they go to video memory at once, then the map is loaded over them */
	if (!cd_load(map_files[idx][1], g_texbuf, sizeof(g_texbuf))) memset(g_texbuf, 0, 8);
	if (world_tex_cb) world_tex_cb();
	{	/* the file's cells start 330 + 12*nents bytes in, 2 off a word boundary: slide the file up 2 bytes so every cell
		   is word aligned (its colour is then one word load) */
		int n = cd_load(map_files[idx][0], g_texbuf, sizeof(g_texbuf) - 2048);
		if (!n) return 0;
		memmove((uint8_t *)g_texbuf + 2, g_texbuf, n);
		hdr = (const uint8_t *)g_texbuf + 2;
	}
	if (memcmp(hdr, "CMP2", 4) != 0) return 0;
	ssize = 1 << hdr[4];
	nents = hdr[6] | (hdr[7] << 8);
	ents = (const MapEnt *)(hdr + 330);
	cells = (Cell *)(hdr + 330 + 12 * nents);
	world_sfactor = hdr[4];
	if (ssize > MAXS) return 0;
	if (!build_sqrs((uint8_t *)(cells + ssize * ssize), (uint8_t *)g_texbuf + sizeof(g_texbuf))) return 0;
	build_mapmodels();
	loaded_idx = idx;
	return 1;
}

int world_ssize(void) { return ssize; }
int world_sky(void) {                         /* 1 = space: Facing Cubes, and the XENO pack's night maps (the stars) */
	static const char *const night[] = { "fcubes", "xeno03", "xeno07", "xdm3" }; unsigned i;
	if (loading_idx < 0) return 0;
	for (i = 0; i < sizeof(night) / sizeof(night[0]); i++) if (!strcmp(map_names[loading_idx], night[i])) return 1;
	return 0;
}
int world_generation(void) { return generation; }
const uint8_t *world_texpack(void) { return (const uint8_t *)g_texbuf; }      /* valid only inside world_tex_cb */
const char *world_title(void) { return (const char *)(hdr + 10); }
int world_water(void) { int w = (int16_t)(hdr[8] | (hdr[9] << 8)); return w < -127 ? NO_WATER : w; }
int world_nents(void) { return nents; }

/* Cube's default mapmodel slots (data/default_map_settings.cfg "mapmodel rad h zoff snap name"), in cube units */
static const int8_t mminfo[NUM_MAPMODELS][3] = {
	{ 1, 8, 0 } /* tree1 */, { 4, 3, 4 } /* bridge */, { 6, 12, 0 } /* tree2 */, { 2, 8, 0 } /* tree3 */, { 1, 9, 0 } /* palmtree */,
	{ 1, 4, 0 } /* thorns */, { 1, 2, 0 } /* plant1 */, { 1, 0, 0 } /* grass */, { 3, 0, 0 } /* ivy */, { 0, 0, 0 } /* flag */, { 0, 0, 0 } /* ammo box */ };
MapModel world_mm[MAXMM]; int world_nmm;
static void build_mapmodels(void) {
	int i;
	world_nmm = 0;
	for (i = 0; i < nents && world_nmm < MAXMM; i++) {
		const MapEnt *e = &ents[i]; MapModel *m; int a;
		if (e->type != E_MAPMODEL || e->attr2 >= NUM_MAPMODELS || (unsigned)e->x >= (unsigned)ssize || (unsigned)e->y >= (unsigned)ssize) continue;
		m = &world_mm[world_nmm++];
		m->mdl = e->attr2; m->x = e->x * FX; m->y = e->y * FX;          /* at the lattice point, as Cube draws it */
		m->z = (world_cell(e->x, e->y)->floor + mminfo[e->attr2][2] + e->attr3) * FX;
		m->rad = mminfo[e->attr2][0] * FX; m->h = mminfo[e->attr2][1] * FX;
		a = e->attr1 + 7; a -= a % 15;                                   /* Cube snaps the angle to 15 degrees */
		m->yaw = (a * 4096 / 360) & 4095;
	}
}
const MapEnt *world_ent(int i) { return &ents[i]; }

/* Cube's settag (world.cpp): tag != 0 opens every cell carrying that tag; tag == 0 resets all tagged cells, to solid
 * (type != 0, the state a map starts in) or to space.  The changed area is remipped so the renderer sees it at once. */
void world_settag(int tag, int type) {
	int x, y, minx = ssize, miny = ssize, maxx = -1, maxy = -1;
	Cell *cw = cells;                             /* the map's cells are level 0 of the mips: one write does both */
	if (!cells) return;
	for (y = 0; y < ssize; y++) for (x = 0; x < ssize; x++) {
		int i = y * ssize + x; Cell *c = &cw[i];
		if (!c->tag) continue;
		if (tag) { if (c->tag != tag) continue; c->type = SPACE_T; }
		else c->type = type ? SOLID_T : SPACE_T;
		if (x < minx) minx = x;
		if (y < miny) miny = y;
		if (x > maxx) maxx = x;
		if (y > maxy) maxy = y;
	}
	if (maxx >= 0) world_remip(minx, miny, maxx - minx + 1, maxy - miny + 1);
}

const Cell *world_cell(int cx, int cy) {
	if (cx < 0 || cy < 0 || cx >= ssize || cy >= ssize) return &solid_cell;
	return &cells[cy * ssize + cx];
}

/* Cube's cornertest: collide with a (possibly mip-mapped) corner square; reports the corner block it belongs to */
static int cornertest(int mip, int x, int y, int dx, int dy, int *bx, int *by, int *bs) {
	int stest = sq(mip, x + dx, y)->type == SOLID_T && sq(mip, x, y + dy)->type == SOLID_T;
	mip++; x /= 2; y /= 2;
	if (mip <= world_sfactor && sq(mip, x, y)->type == CORNER_T) {
		*bx = x << mip; *by = y << mip; *bs = 1 << mip;
		return cornertest(mip, x, y, dx, dy, bx, by, bs);
	}
	return stest;
}

/* The map part of Cube's collide(): the body's square footprint against every cell it covers.  Solid cells block, the solid
 * half of a corner blocks, and a height field counts at the mean height of its four corners (as in Cube). */
int world_fits(int x, int y, int rad, int zfeet, int stepmax, int *floorz, int *ceilz) {
	int fx1 = x - rad, fx2 = x + rad, fy1 = y - rad, fy2 = y + rad;
	int x1 = fx1 >> 8, x2 = fx2 >> 8, y1 = fy1 >> 8, y2 = fy2 >> 8;
	int maxf = -100000, minc = 100000, cx, cy;
	for (cy = y1; cy <= y2; cy++) {
		for (cx = x1; cx <= x2; cx++) {
			const Cell *c = world_cell(cx, cy);
			int f, ce;
			if (cx < 2 || cy < 2 || cx >= ssize - 2 || cy >= ssize - 2) return 0;
			f = c->floor * FX; ce = c->ceil * FX;
			switch (c->type) {
			case SOLID_T: return 0;
			case CORNER_T: {
				int bx = cx, by = cy, bs = 1;
				if ((cx == x1 && cy == y1 && cornertest(0, cx, cy, -1, -1, &bx, &by, &bs) && (fx1 - bx * FX) + (fy1 - by * FX) <= bs * FX)
				 || (cx == x2 && cy == y1 && cornertest(0, cx, cy, 1, -1, &bx, &by, &bs) && (fx2 - bx * FX) >= (fy1 - by * FX))
				 || (cx == x1 && cy == y2 && cornertest(0, cx, cy, -1, 1, &bx, &by, &bs) && (fx1 - bx * FX) <= (fy2 - by * FX))
				 || (cx == x2 && cy == y2 && cornertest(0, cx, cy, 1, 1, &bx, &by, &bs) && (fx2 - bx * FX) + (fy2 - by * FX) >= bs * FX))
					return 0;
				break; }
			case FHF_T:
				f -= (c->vdelta + world_cell(cx + 1, cy)->vdelta + world_cell(cx, cy + 1)->vdelta + world_cell(cx + 1, cy + 1)->vdelta) * (FX / 16);
				break;
			case CHF_T:
				ce += (c->vdelta + world_cell(cx + 1, cy)->vdelta + world_cell(cx, cy + 1)->vdelta + world_cell(cx + 1, cy + 1)->vdelta) * (FX / 16);
				break;
			default: break;
			}
			if (f > zfeet + stepmax) return 0;
			if (f > maxf) maxf = f;
			if (ce < minc) minc = ce;
		}
	}
	{   /* map props (Cube's mmcollide) */
		int i;
		for (i = 0; i < world_nmm; i++) {
			const MapModel *mm = &world_mm[i]; int r;
			if (!mm->h) continue;
			r = mm->rad + rad;
			if (x - mm->x >= r || mm->x - x >= r || y - mm->y >= r || mm->y - y >= r) continue;
			if (zfeet < mm->z) { if (mm->z < minc) minc = mm->z; }
			else { int top = mm->z + mm->h; if (top > zfeet + stepmax) return 0; if (top > maxf) maxf = top; }
		}
	}
	if (minc - maxf < BODYH) return 0;
	if (floorz) *floorz = maxf;
	if (ceilz) *ceilz = minc;
	return 1;
}

int world_ray(int x, int y, int z, int dx, int dy, int dz, int maxdist) {
	int d;
	for (d = 32; d < maxdist; d += 32) {
		int px = x + ((dx * d) >> 12), py = y + ((dy * d) >> 12), pz = z + ((dz * d) >> 12);
		const Cell *c = world_cell(px >> 8, py >> 8);
		if (c->type == SOLID_T || pz < c->floor * FX || pz > c->ceil * FX) return d;
	}
	return maxdist;
}

int world_los(int x0, int y0, int z0, int x1, int y1, int z1) {
	int dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
	int len = vlen3(dx, dy, dz), n, i;
	int sx, sy, sz, px, py, pz;
	if (len < 64) return 1;
	n = len / 48 + 1;
	sx = (dx * 256) / n; sy = (dy * 256) / n; sz = (dz * 256) / n;      /* 8.8 fixed point steps: no division inside the loop */
	px = x0 * 256; py = y0 * 256; pz = z0 * 256;
	for (i = 1; i < n; i++) {
		const Cell *c;
		px += sx; py += sy; pz += sz;
		c = world_cell(px >> 16, py >> 16);
		if (c->type == SOLID_T || (pz >> 8) < c->floor * FX || (pz >> 8) > c->ceil * FX) return 0;
	}
	return 1;
}
