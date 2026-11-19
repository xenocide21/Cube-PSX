/* Cube PSX renderer.
 *
 * The world is drawn the way Cube itself draws it (worldrender.cpp): a top-down walk of the mip quad tree, where a
 * "perfect mip" square is drawn as one big quad and everything else from the level below, with Cube's own rules for
 * floors, ceilings, walls, corners and height fields.  What is PlayStation specific:
 *
 *  - The walk goes strictly front to back.  Solid squares are entered in a 360 degree angular occlusion buffer as they
 *    are met, and a square whose whole angular span is already covered is skipped with everything inside it.  Nothing
 *    hidden is drawn and nothing visible is skipped, so there are no holes to the sky.
 *  - There is no Z buffer, so the walk order is the draw order: every primitive is pushed on the front of one list,
 *    which the GPU then draws back to front.  Monsters and items are slotted into that list when the walk reaches the
 *    first square they stand on.
 *  - Polygons are projected by the GTE.  Anything the GTE cannot project exactly (closer than H/2, or far off screen)
 *    takes a software path that clips in view space and rounds exactly like the GTE, so shared edges still meet.
 *
 * World (Cube) -> PS1 mapping: X = cubeX, Z = cubeY, Y = -cubeZ; 1 cube unit = 64 PS1 units. */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxapi.h>
#include <psxetc.h>
#include <inline_c.h>
#include "tables.h"

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define VIEW_L 0                   /* 3D view window: 320x184, the status bar takes the rest */
#define VIEW_R 320
#define VIEW_T 0
#define VIEW_B 184
#define BAR_Y 184                  /* status bar: 184..240 */
#define CENTERX ((VIEW_L + VIEW_R) >> 1)
#define CENTERY ((VIEW_T + VIEW_B) >> 1)
#define PROJ_H 160                 /* GTE projection distance: 90 degree field of view across 320 pixels */
#define NEARZ 16                   /* software near plane (PS units) */
#define PACKET_LEN 110000
#define CORPSE_TICKS (8 * 60)
#define FRAME_VBLANKS 2             /* frame pacing: 2 vblanks = a steady 30 fps (NTSC) */
#define FAR_MAX 72                 /* view distance in cells: fog reaches black there */
#define FAR_MIN 20
#define LOD_K 3                    /* a 2^n square may be drawn as one quad from LOD_K * 2^n cells away */
#define MAX_LEAF 3                 /* biggest square drawn as one quad: 8x8 cells */

enum { OT_END, OT_HUD, OT_GUN, OT_WORLD, OT_SKY, OT_LEN };

typedef struct { DISPENV disp; DRAWENV draw; uint32_t ot[OT_LEN]; char p[PACKET_LEN]; } DB;
static DB db[2];
static int dbi;
static uint32_t *ot;                   /* current ordering table */
static char *nextpri, *endpri;
static MATRIX cam;
static int frame_polys, dropped_polys;
static int far_cells = 48;             /* adaptive view distance */
int dbg_far = FAR_MAX;                 /* the rig sets it, with dbg_flags 8 */
static int sky_cur = -1;               /* which sky box is loaded: 0 Cube's, 1 space */
static int underwater;

#define LINK(slot, p, len) do { *(uint32_t *)(p) = ((uint32_t)(len) << 24) | ot[slot]; ot[slot] = (uint32_t)(p) & 0xffffff; } while (0)

static void models_init(void);
static void sky_init(void);
static void ui_load(void);
static void flip(void);

static void tex_upload(void);
static void props_tex(void);
static void sky_for_map(void);
void render_init(void) {
	world_tex_cb = tex_upload;
	ResetGraph(0);
	cd_init();
	SetDefDispEnv(&db[0].disp, 0, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[0].draw, SCREEN_XRES, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDispEnv(&db[1].disp, SCREEN_XRES, 0, SCREEN_XRES, SCREEN_YRES);
	SetDefDrawEnv(&db[1].draw, 0, 0, SCREEN_XRES, SCREEN_YRES);
	setRGB0(&db[0].draw, 0, 0, 0); db[0].draw.isbg = 1; db[0].draw.dtd = 1;
	setRGB0(&db[1].draw, 0, 0, 0); db[1].draw.isbg = 1; db[1].draw.dtd = 1;
	PutDrawEnv(&db[0].draw);
	ClearOTagR(db[0].ot, OT_LEN); ClearOTagR(db[1].ot, OT_LEN);
	ot = db[0].ot; nextpri = db[0].p; endpri = db[0].p + PACKET_LEN - 256;
	InitGeom();
	gte_SetGeomOffset(CENTERX, CENTERY);
	gte_SetGeomScreen(PROJ_H);
	FntLoad(960, 0);
	FntOpen(60, 190, 200, 48, 0, 128);          /* stream 0: status bar text */
	FntOpen(8, 8, 304, 168, 0, 128);            /* stream 1: messages / loading, inside the 3D view */
	models_init();
	sky_init();
	ui_load();
}

void render_loading(const char *what) {
	int i;
	for (i = 0; i < 2; i++) { FntPrint(1, "\n\n\n\n\n\n\n\n\n      LOADING %s ...\n", what); FntFlush(1); flip(); }
}

/* ---------- frame timing ---------- */
static int fps_now, fps_frames, fps_start, last_dv = 2, flip_vb, build_hb, diag_world, diag_things, diag_hud;
static int hb_now(void) { return VSync(1); }                      /* hblanks since the last VSync(0) returned (263 per frame) */

static void flip(void) {
	int vb;
	build_hb = hb_now();                                          /* everything since the last flip: game logic + frame build */
	DrawSync(0);
	VSync(0);
	while (VSync(-1) - flip_vb < FRAME_VBLANKS) VSync(0);         /* steady pace: never show a frame for less than FRAME_VBLANKS */
	fps_frames++; vb = VSync(-1); last_dv = vb - flip_vb; flip_vb = vb;
	if (vb - fps_start >= 60) { fps_now = (fps_frames * 60 + (vb - fps_start) / 2) / (vb - fps_start); fps_frames = 0; fps_start = vb; }
	dbi ^= 1;
	PutDrawEnv(&db[dbi].draw); PutDispEnv(&db[dbi].disp);
	SetDispMask(1);
	DrawOTag(db[1 - dbi].ot + (OT_LEN - 1));
	ot = db[dbi].ot; nextpri = db[dbi].p; endpri = db[dbi].p + PACKET_LEN - 256;
	ClearOTagR(ot, OT_LEN);
}

/* ---------- textures ---------- */
typedef struct { uint8_t ok, lw, lh, pad; uint16_t uvbase; uint32_t clutw, tpagew; } TexInfo;
static TexInfo texinfo[256];
static int tex_generation = -1;
/* the liquid texture (TEX3 slot 255, 16 colours, every colour see-through): where it is in VRAM and its pixels, so it
   can be scrolled by uploading it shifted (Cube drifts the water texture a quarter of a cell a second) */
static const uint8_t *water_px; static RECT water_vram; static int water_du, water_dv;
static uint8_t water_buf[8192], water_src[8192];      /* (the pack's memory is reused for the map: keep the pixels) */

static void tex_set(TexInfo *t, int X, int Y, int sub, int cx, int cy, int lw, int lh) {
	t->ok = 1; t->lw = lw; t->lh = lh;
	t->uvbase = ((sub & 1) * 128) | (((sub >> 1) * 128) << 8);
	t->tpagew = (uint32_t)getTPage(0, 0, X, Y) << 16; t->clutw = (uint32_t)getClut(cx, cy) << 16;
}

static void tex_load(const uint8_t *px, int X, int Y, int sub, int cx, int cy) {
	RECT r, rc;
	r.x = X + (sub & 1) * 32; r.y = Y + (sub >> 1) * 128; r.w = 32; r.h = 128;
	LoadImage(&r, (const uint32_t *)px); DrawSync(0);
	rc.x = cx; rc.y = cy; rc.w = 16; rc.h = 1;
	LoadImage(&rc, (const uint32_t *)(px + 8192)); DrawSync(0);
}

/* World textures (TEX3 pack): 12 texture pages = 48 quarter-page units.  A 256-colour texture takes half a page (two
 * units), a 16-colour one a quarter; the pack lists the 256-colour ones first. */
static int tex_got = -1, tex_want;       /* world textures loaded / in the pack (-1 = the pack would not load): shown when short */
static void tex_upload(void) {
	static const int16_t wpage[12][2] = { { 640, 0 }, { 704, 0 }, { 768, 0 }, { 832, 0 }, { 896, 0 }, { 640, 256 }, { 704, 256 }, { 768, 256 }, { 832, 256 }, { 896, 256 }, { 512, 256 }, { 576, 256 } };
	const uint8_t *p = world_texpack();
	int i, n, unit = 0, n8 = 0, n4 = 0;
	memset(texinfo, 0, sizeof(texinfo));
	water_px = 0;
	tex_generation = world_generation();
	tex_got = -1; tex_want = 0;
	if (!p || memcmp(p, "TEX3", 4) != 0) { props_tex(); return; }
	n = p[4] | (p[5] << 8); p += 8; tex_got = 0; tex_want = n;
	DrawSync(0);
	for (i = 0; i < n; i++) {
		int slot = p[0], lw = p[1], lh = p[2], bpp = p[3]; RECT r; TexInfo *t = &texinfo[slot];
		p += 4;
		if (bpp == 8) {
			int page, half, cx = (n8 & 1) * 256, cy = 242 + (n8 >> 1);
			unit = (unit + 1) & ~1; page = unit >> 2; half = (unit >> 1) & 1;
			if (unit + 2 > 48 || n8 >= 28) break;
			r.x = wpage[page][0]; r.y = wpage[page][1] + half * 128; r.w = 64; r.h = 128;
			LoadImage(&r, (const uint32_t *)p); DrawSync(0);
			r.x = cx; r.y = cy; r.w = 256; r.h = 1;
			LoadImage(&r, (const uint32_t *)(p + 16384)); DrawSync(0);
			t->ok = 1; t->lw = lw; t->lh = lh; t->uvbase = (half * 128) << 8;
			t->tpagew = (uint32_t)getTPage(1, 0, wpage[page][0], wpage[page][1]) << 16; t->clutw = (uint32_t)getClut(cx, cy) << 16;
			unit += 2; n8++; p += 16384 + 512; tex_got++;
		} else {
			int page = unit >> 2, sub = unit & 3;
			if (unit + 1 > 48 || n4 >= 40) break;
			if (slot == 255) { memcpy(water_src, p, 8192); water_px = water_src; water_vram.x = wpage[page][0] + (sub & 1) * 32; water_vram.y = wpage[page][1] + (sub >> 1) * 128; water_vram.w = 32; water_vram.h = 128; water_du = water_dv = -1; }
			tex_load(p, wpage[page][0], wpage[page][1], sub, n4 * 16, 240);
			tex_set(t, wpage[page][0], wpage[page][1], sub, n4 * 16, 240, lw, lh);
			unit++; n4++; p += 8192 + 32; tex_got++;
		}
	}
	props_tex();                          /* map props: their skins are slots 240 + prop number of this pack */
	sky_for_map();
}

/* ---------- light and fog ----------
 * A vertex colour is Cube's own light value: 128 shows the texture as it is and 255 at double brightness, the same x2
 * "overbright" Cube draws the world with.  Fog scales it to black over the last part of the view distance, so nothing
 * pops in at the far edge. */
static uint16_t fogtab[128];                      /* per cell of distance: 256 = unfogged .. 0 */
static int fog_far = -1;
int dbg_flags;                                    /* test rig switches, see node() */
static int fog_off;                               /* set while drawing the sky */
static void fog_setup(void) {
	int i, z0 = far_cells * 5 / 8;
	if (fog_far == far_cells) return;
	fog_far = far_cells;
	for (i = 0; i < 128; i++) fogtab[i] = i <= z0 ? 256 : i >= far_cells ? 0 : 256 - (i - z0) * 256 / (far_cells - z0);
}
static inline int fog_at(int z) {                 /* z in PS units -> 0..256 */
	int i = z >> 6;
	if (fog_off || (dbg_flags & 32)) return 256;
	return fogtab[i > 127 ? 127 : i];
}
static inline uint32_t fog_col(uint32_t c, int f) {      /* scale a packed r,g,b by f / 256 */
	return ((((c & 0xff00ff) * f) >> 8) & 0xff00ff) | ((((c & 0x00ff00) * f) >> 8) & 0x00ff00);
}

/* ---------- projection helpers shared by the software path ---------- */
#define FLAG_BAD 0x00066000u           /* GTE: divide overflow (closer than H/2), SX/SY saturated, SZ out of range */
static inline uint32_t gte_flags(void) { uint32_t v; __asm__ volatile("cfc2 %0, $31" : "=r"(v)); return v; }

static inline int gte_recip(int z) {   /* the GTE's own H/z (16.16): the UNR divider, bit for bit, valid for z > H/2 */
	int sh = 0; unsigned n, d, u;
	if (z <= PROJ_H / 2) return (PROJ_H << 16) / z;
	d = (unsigned)z;
	if (!(d & 0xff00)) { sh += 8; d <<= 8; }
	if (!(d & 0xf000)) { sh += 4; d <<= 4; }
	if (!(d & 0xc000)) { sh += 2; d <<= 2; }
	if (!(d & 0x8000)) { sh += 1; d <<= 1; }
	n = (unsigned)PROJ_H << sh;
	u = unr_table[(d - 0x7fc0) >> 7] + 0x101;
	d = (0x2000080 - d * u) >> 8;
	d = (0x0000080 + d * u) >> 8;
	n = (unsigned)(((unsigned long long)n * d + 0x8000) >> 16);
	return n > 0x1ffff ? 0x1ffff : (int)n;
}

typedef struct { int x, y, z, u, v, r, g, b; } CV;      /* view-space vertex with texture coordinates and light */

/* keep a*x + b*y + c*z + k >= 0.  Coordinates are in 1/16 units (so up to about 2^19) and u,v in 1/16 texels: a vertex
 * made by the clip has to land within a fraction of a pixel of the true edge, or the seam to the unclipped neighbour opens. */
static int clip_poly(CV *p, int n, int a, int b, int c, int k) {
	CV o[10]; int m = 0, i;
	for (i = 0; i < n; i++) {
		const CV *s = &p[i], *e = &p[i + 1 == n ? 0 : i + 1];
		int ds = a * s->x + b * s->y + c * s->z + k, de = a * e->x + b * e->y + c * e->z + k;
		if (ds >= 0) o[m++] = *s;
		if ((ds >= 0) != (de >= 0)) {
			int t, dd = ds - de, dn = ds;
			while (dn > 0x1ffff || dn < -0x1ffff || dd > 0x1ffff || dd < -0x1ffff) { dn >>= 1; dd >>= 1; }      /* keep dn << 14 inside 32 bits */
			t = dd ? (dn << 14) / dd : 0;
			o[m].x = s->x + (int)(((long long)(e->x - s->x) * t) >> 14); o[m].y = s->y + (int)(((long long)(e->y - s->y) * t) >> 14);
			o[m].z = s->z + (int)(((long long)(e->z - s->z) * t) >> 14);
			o[m].u = s->u + (((e->u - s->u) * t) >> 14); o[m].v = s->v + (((e->v - s->v) * t) >> 14);
			o[m].r = s->r + (((e->r - s->r) * t) >> 14); o[m].g = s->g + (((e->g - s->g) * t) >> 14); o[m].b = s->b + (((e->b - s->b) * t) >> 14); m++;
		}
	}
	for (i = 0; i < m; i++) p[i] = o[i];
	return m;
}

static uint32_t semi_cmd;              /* 0x02000000 while drawing water: see-through (50/50), and no 1-pixel growth */

/* The exact but slow way to draw a world polygon (n = 3 or 4 corners, in cyclic order): view-space clip, then project. */
static void poly_slow(const SVECTOR *const *v, int n, const uint16_t *uv, const TexInfo *t, const uint32_t *lt, int slot) {
	CV p[10]; int i, zs = 0, f, sx[10], sy[10], out = 0, clipped = 0, tu[10], tv[10]; uint32_t col[10];
	for (i = 0; i < n; i++) {
		const SVECTOR *s = v[i];
		p[i].x = (((cam.t[0] << 12) + cam.m[0][0] * s->vx + cam.m[0][1] * s->vy + cam.m[0][2] * s->vz) >> 12) << 4;
		p[i].y = (((cam.t[1] << 12) + cam.m[1][0] * s->vx + cam.m[1][1] * s->vy + cam.m[1][2] * s->vz) >> 12) << 4;
		p[i].z = (((cam.t[2] << 12) + cam.m[2][0] * s->vx + cam.m[2][1] * s->vy + cam.m[2][2] * s->vz) >> 12) << 4;
		p[i].u = (uv[i] & 255) << 4; p[i].v = (uv[i] >> 8) << 4;
		p[i].r = lt[i] & 255; p[i].g = (lt[i] >> 8) & 255; p[i].b = (lt[i] >> 16) & 255;
		if (p[i].z < NEARZ << 4) out |= 1;
	}
	if (out) { n = clip_poly(p, n, 0, 0, 1, -(NEARZ << 4)); if (n < 3) return; for (i = 0; i < n; i++) if (p[i].z < NEARZ << 4) p[i].z = NEARZ << 4; clipped = 1; }
	/* guard band: keep every vertex within 500 px (x) and 250 px (y) of the centre, as the GPU drops polygons wider than 1023 or taller than 511 */
	out = 0;
	for (i = 0; i < n; i++) {
		if (8 * p[i].x > 25 * p[i].z) out |= 1;
		if (-8 * p[i].x > 25 * p[i].z) out |= 2;
		if (16 * p[i].y > 25 * p[i].z) out |= 4;
		if (-16 * p[i].y > 25 * p[i].z) out |= 8;
	}
	if (out & 1) { n = clip_poly(p, n, -8, 0, 25, 0); if (n < 3) return; }
	if (out & 2) { n = clip_poly(p, n, 8, 0, 25, 0); if (n < 3) return; }
	if (out & 4) { n = clip_poly(p, n, 0, -16, 25, 0); if (n < 3) return; }
	if (out & 8) { n = clip_poly(p, n, 0, 16, 25, 0); if (n < 3) return; }
	if (out) clipped = 1;
	out = 15;
	for (i = 0; i < n; i++) {
		int c = 0, z = p[i].z, r;
		/* an original vertex is projected exactly as the GTE does it (its neighbours were); a vertex made by the clip gets a full-precision divide */
		if (!clipped || !(z & 15)) { r = gte_recip(z >> 4 < 1 ? 1 : z >> 4); sx[i] = CENTERX + (int)(((long long)r * (p[i].x >> 4)) >> 16); sy[i] = CENTERY + (int)(((long long)r * (p[i].y >> 4)) >> 16); }
		else { r = (PROJ_H << 20) / z; sx[i] = CENTERX + (int)(((long long)r * p[i].x) >> 20); sy[i] = CENTERY + (int)(((long long)r * p[i].y) >> 20); }
		zs += z >> 4;
		tu[i] = (p[i].u + 8) >> 4; tv[i] = (p[i].v + 8) >> 4;
		if (sx[i] < VIEW_L) c |= 1;
		if (sx[i] >= VIEW_R) c |= 2;
		if (sy[i] < VIEW_T) c |= 4;
		if (sy[i] >= VIEW_B) c |= 8;
		out &= c;
	}
	if (out) return;
	{
		int cx = 0, cy = 0;
		for (i = 0; i < n; i++) { cx += sx[i]; cy += sy[i]; }
		cx /= n; cy /= n;
		for (i = 0; i < n; i++) {
			if (!semi_cmd) { sx[i] += ((sx[i] - cx) >> 31) | 1; sy[i] += ((sy[i] - cy) >> 31) | 1; }
			/* the clip above put every vertex inside the guard band up to rounding; make that a guarantee for the GPU's size limit */
			if (sx[i] < CENTERX - 505) sx[i] = CENTERX - 505;
			if (sx[i] > CENTERX + 505) sx[i] = CENTERX + 505;
			if (sy[i] < CENTERY - 253) sy[i] = CENTERY - 253;
			if (sy[i] > CENTERY + 253) sy[i] = CENTERY + 253;
		}
	}
	f = t ? fog_at(zs / n) : 256;                   /* (fully fogged polygons are still drawn, black: they have to hide the sky box) */
	for (i = 0; i < n; i++) col[i] = fog_col(p[i].r | (p[i].g << 8) | (p[i].b << 16), f);
	if (!t) {                                       /* untextured: missing texture, or the black wall that stands in for everything past the view distance */
		for (i = 1; i + 1 < n; i++) {
			uint32_t *q = (uint32_t *)nextpri;
			if (nextpri + 28 > endpri) { dropped_polys++; return; }
			q[1] = 0x30000000 | col[0]; q[2] = (sx[0] & 0xffff) | (sy[0] << 16);
			q[3] = col[i]; q[4] = (sx[i] & 0xffff) | (sy[i] << 16);
			q[5] = col[i + 1]; q[6] = (sx[i + 1] & 0xffff) | (sy[i + 1] << 16);
			LINK(slot, q, 6); nextpri += 28; frame_polys++;
		}
		return;
	}
	for (i = 1; i + 1 < n; i += 2) {
		uint32_t *q = (uint32_t *)nextpri;
		if (nextpri + 52 > endpri) { dropped_polys++; return; }
		if (i + 2 < n) {                            /* two triangles of the fan as one quad: 0, i, i+1, i+2 */
			q[1] = 0x3c000000 | semi_cmd | col[0];
			q[2] = (sx[0] & 0xffff) | (sy[0] << 16);          q[3] = (t->uvbase + (tu[0] | (tv[0] << 8))) | t->clutw;
			q[4] = col[i];     q[5] = (sx[i] & 0xffff) | (sy[i] << 16);          q[6] = (t->uvbase + (tu[i] | (tv[i] << 8))) | t->tpagew;
			q[7] = col[i + 2]; q[8] = (sx[i + 2] & 0xffff) | (sy[i + 2] << 16);  q[9] = t->uvbase + (tu[i + 2] | (tv[i + 2] << 8));
			q[10] = col[i + 1]; q[11] = (sx[i + 1] & 0xffff) | (sy[i + 1] << 16); q[12] = t->uvbase + (tu[i + 1] | (tv[i + 1] << 8));
			LINK(slot, q, 12); nextpri += 52;
		} else {
			q[1] = 0x34000000 | semi_cmd | col[0];
			q[2] = (sx[0] & 0xffff) | (sy[0] << 16);          q[3] = (t->uvbase + (tu[0] | (tv[0] << 8))) | t->clutw;
			q[4] = col[i];     q[5] = (sx[i] & 0xffff) | (sy[i] << 16);          q[6] = (t->uvbase + (tu[i] | (tv[i] << 8))) | t->tpagew;
			q[7] = col[i + 1]; q[8] = (sx[i + 1] & 0xffff) | (sy[i + 1] << 16);  q[9] = t->uvbase + (tu[i + 1] | (tv[i + 1] << 8));
			LINK(slot, q, 9); nextpri += 40;
		}
		frame_polys++;
	}
}

/* shared tail of the fast paths: reject what is off screen, send what the GPU could not take to the slow path, and grow
 * the polygon by one pixel all round (where a big square meets small ones - a T junction - the integer screen
 * coordinates would otherwise leave one-pixel cracks that show whatever is behind) */
#define OFFSCREEN4(x0, y0, x1, y1, x2, y2, x3, y3) \
	(((x0) < VIEW_L && (x1) < VIEW_L && (x2) < VIEW_L && (x3) < VIEW_L) || ((x0) >= VIEW_R && (x1) >= VIEW_R && (x2) >= VIEW_R && (x3) >= VIEW_R) || \
	 ((y0) < VIEW_T && (y1) < VIEW_T && (y2) < VIEW_T && (y3) < VIEW_T) || ((y0) >= VIEW_B && (y1) >= VIEW_B && (y2) >= VIEW_B && (y3) >= VIEW_B))
#define TOOBIG(x, y) ((unsigned)((x) - CENTERX + 500) > 1000u || (unsigned)((y) - CENTERY + 250) > 500u)
#define GROW(x, y, cx, cy) ((((x) + ((((x) - (cx)) >> 31) | 1)) & 0xffff) | (((y) + ((((y) - (cy)) >> 31) | 1)) << 16))

/* Textured quad, corners a b / c d in "N" order; uv = four (u | v << 8) inside the texture's 128x128 tile; lt = the four
 * vertex lights (packed r,g,b).  Flat shaded when the four lights agree, gouraud otherwise. */
static void wquad(const SVECTOR *a, const SVECTOR *b, const SVECTOR *c, const SVECTOR *d, const uint16_t *uv, const TexInfo *t, const uint32_t *lt, int slot) {
	uint32_t *q = (uint32_t *)nextpri, f1, f2, xy[4]; int otz, f, x0, y0, x1, y1, x2, y2, x3, y3, cx, cy;
	if (nextpri + 52 > endpri) { dropped_polys++; return; }
	gte_ldv3(a, b, c);
	gte_rtpt();
	f1 = gte_flags();
	gte_stsxy3(&xy[0], &xy[1], &xy[2]);
	gte_ldv0(d);
	gte_rtps();
	f2 = gte_flags();
	gte_stsxy(&xy[3]);
	if (!t || ((f1 | f2) & FLAG_BAD)) goto slow;
	x0 = (int16_t)xy[0]; y0 = (int32_t)xy[0] >> 16; x1 = (int16_t)xy[1]; y1 = (int32_t)xy[1] >> 16;
	x2 = (int16_t)xy[2]; y2 = (int32_t)xy[2] >> 16; x3 = (int16_t)xy[3]; y3 = (int32_t)xy[3] >> 16;
	if (OFFSCREEN4(x0, y0, x1, y1, x2, y2, x3, y3)) return;
	if (TOOBIG(x0, y0) || TOOBIG(x1, y1) || TOOBIG(x2, y2) || TOOBIG(x3, y3)) goto slow;
	gte_avsz4();
	gte_stotz(&otz);
	f = fog_at(otz << 2);
	cx = (x0 + x1 + x2 + x3) >> 2; cy = (y0 + y1 + y2 + y3) >> 2;
	if (semi_cmd) {                                  /* water: see-through, exact edges (a 1-pixel overlap would blend twice) */
		q[1] = 0x3e000000 | fog_col(lt[0], f); q[4] = fog_col(lt[1], f); q[7] = fog_col(lt[2], f); q[10] = fog_col(lt[3], f);
		q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = (t->uvbase + uv[0]) | t->clutw;
		q[5] = (x1 & 0xffff) | (y1 << 16); q[6] = (t->uvbase + uv[1]) | t->tpagew;
		q[8] = (x2 & 0xffff) | (y2 << 16); q[9] = t->uvbase + uv[2];
		q[11] = (x3 & 0xffff) | (y3 << 16); q[12] = t->uvbase + uv[3];
		LINK(slot, q, 12); nextpri += 52; frame_polys++;
		return;
	}
	if (lt[0] == lt[1] && lt[0] == lt[2] && lt[0] == lt[3]) {
		q[1] = 0x2c000000 | (f == 256 ? lt[0] : fog_col(lt[0], f));
		q[2] = GROW(x0, y0, cx, cy); q[3] = (t->uvbase + uv[0]) | t->clutw;
		q[4] = GROW(x1, y1, cx, cy); q[5] = (t->uvbase + uv[1]) | t->tpagew;
		q[6] = GROW(x2, y2, cx, cy); q[7] = t->uvbase + uv[2];
		q[8] = GROW(x3, y3, cx, cy); q[9] = t->uvbase + uv[3];
		LINK(slot, q, 9); nextpri += 40;
	} else {
		if (f == 256) { q[1] = 0x3c000000 | lt[0]; q[4] = lt[1]; q[7] = lt[2]; q[10] = lt[3]; }
		else { q[1] = 0x3c000000 | fog_col(lt[0], f); q[4] = fog_col(lt[1], f); q[7] = fog_col(lt[2], f); q[10] = fog_col(lt[3], f); }
		q[2] = GROW(x0, y0, cx, cy); q[3] = (t->uvbase + uv[0]) | t->clutw;
		q[5] = GROW(x1, y1, cx, cy); q[6] = (t->uvbase + uv[1]) | t->tpagew;
		q[8] = GROW(x2, y2, cx, cy); q[9] = t->uvbase + uv[2];
		q[11] = GROW(x3, y3, cx, cy); q[12] = t->uvbase + uv[3];
		LINK(slot, q, 12); nextpri += 52;
	}
	frame_polys++;
	return;
slow:
	{ const SVECTOR *v[4]; uint16_t uvc[4]; uint32_t lc[4];
	  v[0] = a; v[1] = b; v[2] = d; v[3] = c; uvc[0] = uv[0]; uvc[1] = uv[1]; uvc[2] = uv[3]; uvc[3] = uv[2]; lc[0] = lt[0]; lc[1] = lt[1]; lc[2] = lt[3]; lc[3] = lt[2];
	  poly_slow(v, 4, uvc, t, lc, slot); }
}

static void wtri(const SVECTOR *a, const SVECTOR *b, const SVECTOR *c, const uint16_t *uv, const TexInfo *t, const uint32_t *lt, int slot) {
	uint32_t *q = (uint32_t *)nextpri, f1, xy[3]; int otz, f, x0, y0, x1, y1, x2, y2, cx, cy;
	if (nextpri + 40 > endpri) { dropped_polys++; return; }
	gte_ldv3(a, b, c);
	gte_rtpt();
	f1 = gte_flags();
	gte_stsxy3(&xy[0], &xy[1], &xy[2]);
	if (!t || (f1 & FLAG_BAD)) goto slow;
	x0 = (int16_t)xy[0]; y0 = (int32_t)xy[0] >> 16; x1 = (int16_t)xy[1]; y1 = (int32_t)xy[1] >> 16; x2 = (int16_t)xy[2]; y2 = (int32_t)xy[2] >> 16;
	if (OFFSCREEN4(x0, y0, x1, y1, x2, y2, x2, y2)) return;
	if (TOOBIG(x0, y0) || TOOBIG(x1, y1) || TOOBIG(x2, y2)) goto slow;
	gte_avsz3();
	gte_stotz(&otz);
	f = fog_at(otz << 2);
	cx = (x0 + x1 + x2) / 3; cy = (y0 + y1 + y2) / 3;
	q[1] = 0x34000000 | fog_col(lt[0], f); q[4] = fog_col(lt[1], f); q[7] = fog_col(lt[2], f);
	q[2] = GROW(x0, y0, cx, cy); q[3] = (t->uvbase + uv[0]) | t->clutw;
	q[5] = GROW(x1, y1, cx, cy); q[6] = (t->uvbase + uv[1]) | t->tpagew;
	q[8] = GROW(x2, y2, cx, cy); q[9] = t->uvbase + uv[2];
	LINK(slot, q, 9); nextpri += 40; frame_polys++;
	return;
slow:
	{ const SVECTOR *v[3]; v[0] = a; v[1] = b; v[2] = c; poly_slow(v, 3, uv, t, lt, slot); }
}

/* ---------- world geometry: Cube's rendercubes.cpp, in PS1 terms ---------- */
static int vx, vy, vh;                 /* viewer position and eye height (FX) */
static int fwx, fwy, rtx, rty;         /* forward and right unit vectors on the map (4.12) */
static int render_floor, render_ceil, sky_needed;
static int skirt;                      /* world-space overlap (PS units) for the square being drawn: about 1.5 pixels at its distance */
static int ssz;                        /* map size in cells */

static inline const TexInfo *texof(int slot) { return texinfo[slot].ok ? &texinfo[slot] : (const TexInfo *)0; }
#define UV(u, v) ((uint16_t)((u) | ((v) << 8)))

/* floor / ceiling square (level-0 cell coordinates x,y, size cells).  Corner heights in quarter cells and the squares that
 * light the corners, in Cube's order: s=(x,y) t=(x+size,y) u=(x+size,y+size) v=(x,y+size) */
static void flat(int tex, int x, int y, int size, int hs, int ht, int hu, int hv, const Sqr *ls, const Sqr *lt, const Sqr *lu, const Sqr *lv) {
	const TexInfo *t = texof(tex); SVECTOR a, b, c, d; uint16_t uv[4]; uint32_t l[4];
	int x0 = x << 6, x1 = (x + size) << 6, z0 = y << 6, z1 = (y + size) << 6, u0 = 0, u1 = 127, v0 = 0, v1 = 127;
	if (size > 1) { x0 -= skirt; x1 += skirt; z0 -= skirt; z1 += skirt; }      /* a merged square overlaps its neighbours a little (T junctions) */
	a.vx = x0; a.vz = z0; a.vy = -hs * 16;
	b.vx = x1; b.vz = z0; b.vy = -ht * 16;
	c.vx = x0; c.vz = z1; c.vy = -hv * 16;
	d.vx = x1; d.vz = z1; d.vy = -hu * 16;
	if (t) {
		if (size <= (1 << t->lw)) { u0 = ((x & ((1 << t->lw) - 1)) * 128) >> t->lw; u1 = u0 + ((size * 128) >> t->lw) - 1; }
		if (size <= (1 << t->lh)) { v0 = ((y & ((1 << t->lh) - 1)) * 128) >> t->lh; v1 = v0 + ((size * 128) >> t->lh) - 1; }
	}
	uv[0] = UV(u0, v0); uv[1] = UV(u1, v0); uv[2] = UV(u0, v1); uv[3] = UV(u1, v1);
	l[0] = SQLIGHT(ls); l[1] = SQLIGHT(lt); l[2] = SQLIGHT(lv); l[3] = SQLIGHT(lu);
	wquad(&a, &b, &c, &d, uv, t, l, OT_WORLD);
}

/* wall from (x1,y1) to (x2,y2) (level-0 cell coordinates; `size` = length in cells along the map).
 * Bottom / top heights at each end in quarter cells, l1 / l2 = light at each end.  Texture runs as in Cube's render_square. */
static void wall(int tex, int x1, int y1, int x2, int y2, int size, int b1, int b2, int t1, int t2, uint32_t l1, uint32_t l2) {
	const TexInfo *t; SVECTOR a, b, c, d; uint16_t uv[4]; uint32_t l[4];
	int u0 = 0, u1 = 127, th4;
	if (b1 >= t1 && b2 >= t2) return;
	if (tex == 0) { sky_needed = 1; return; }
	t = texof(tex);
	l[0] = l[2] = l1; l[1] = l[3] = l2;
	a.vx = x1 << 6; a.vz = y1 << 6; b.vx = x2 << 6; b.vz = y2 << 6;
	/* walls overlap their neighbours along the wall by the skirt: adjoining walls split at different heights (T junctions) */
	if (x1 < x2) { a.vx -= skirt; b.vx += skirt; } else if (x1 > x2) { a.vx += skirt; b.vx -= skirt; }
	if (y1 < y2) { a.vz -= skirt; b.vz += skirt; } else if (y1 > y2) { a.vz += skirt; b.vz -= skirt; }
	c.vx = a.vx; c.vz = a.vz; d.vx = b.vx; d.vz = b.vz;
	if (!t) { a.vy = -t1 * 16; b.vy = -t2 * 16; c.vy = -b1 * 16; d.vy = -b2 * 16; uv[0] = uv[1] = uv[2] = uv[3] = 0; wquad(&a, &b, &c, &d, uv, t, l, OT_WORLD); return; }
	if (size <= (1 << t->lw)) {
		int along = x1 == x2 ? (y1 < y2 ? y1 : y2) : (x1 < x2 ? x1 : x2);
		u0 = ((along & ((1 << t->lw) - 1)) * 128) >> t->lw; u1 = u0 + ((size * 128) >> t->lw) - 1;
	}
	th4 = 4 << t->lh;                                   /* texture tile height in quarter cells */
	if (b1 == b2 && t1 == t2) {                         /* level wall: one quad per vertical repeat of the texture */
		int top = t1, segs = 0;
		while (top > b1) {
			int idx = top > 0 ? (top - 1) / th4 : -((-top + th4) / th4), tile_top = (idx + 1) * th4, tile_bot = idx * th4;
			int lo = tile_bot > b1 ? tile_bot : b1, vt, vb;
			if (++segs >= 12) lo = b1;
			vt = ((tile_top - top) * 128) / th4;
			vb = ((tile_top - lo) * 128) / th4 - 1;
			if (vb > 127) vb = 127;
			if (vb < vt) vb = vt;
			uv[0] = UV(u0, vt); uv[1] = UV(u1, vt); uv[2] = UV(u0, vb); uv[3] = UV(u1, vb);
			a.vy = b.vy = -top * 16 - (top == t1 ? skirt : 0); c.vy = d.vy = -lo * 16 + (lo == b1 ? skirt : 0);      /* and poke a little into the floor / ceiling */
			wquad(&a, &b, &c, &d, uv, t, l, OT_WORLD);
			top = lo;
		}
	} else {                                            /* sloping top or bottom (next to a height field): a single quad */
		int hi = t1 > t2 ? t1 : t2, idx = hi > 0 ? (hi - 1) / th4 : -((-hi + th4) / th4), tile_top = (idx + 1) * th4, vv[4], i;
		vv[0] = tile_top - t1; vv[1] = tile_top - t2; vv[2] = tile_top - b1; vv[3] = tile_top - b2;
		for (i = 0; i < 4; i++) { vv[i] = (vv[i] * 128) / th4; if (vv[i] > 127) vv[i] = 127; if (vv[i] < 0) vv[i] = 0; }
		uv[0] = UV(u0, vv[0]); uv[1] = UV(u1, vv[1]); uv[2] = UV(u0, vv[2]); uv[3] = UV(u1, vv[3]);
		a.vy = -t1 * 16 - skirt; b.vy = -t2 * 16 - skirt; c.vy = -b1 * 16 + skirt; d.vy = -b2 * 16 + skirt;
		wquad(&a, &b, &c, &d, uv, t, l, OT_WORLD);
	}
}

/* Cube's render_wall: what square `o` shows to its open neighbour `s` along the edge (x1,y1)-(x2,y2); d1/d2 are the squares
 * at the two ends, which carry the height-field deltas and the light. */
static void render_wall(const Sqr *o, const Sqr *s, int x1, int y1, int x2, int y2, int size, const Sqr *d1, const Sqr *d2) {
	if (o->type == SOLID_T || o->type == SEMISOLID_T) {
		int b1 = s->floor * 4, b2 = b1, t1 = s->ceil * 4, t2 = t1;
		if (s->type == FHF_T) { b1 -= d1->vdelta; b2 -= d2->vdelta; }
		if (s->type == CHF_T) { t1 += d1->vdelta; t2 += d2->vdelta; }
		wall(o->wtex, x1, y1, x2, y2, size, b1, b2, t1, t2, SQLIGHT(d1), SQLIGHT(d2));
		return;
	}
	{
		int b1 = s->floor * 4, b2 = b1, t1 = o->floor * 4, t2 = t1;
		if (o->type == FHF_T && s->type != FHF_T) { t1 -= d1->vdelta; t2 -= d2->vdelta; }
		if (s->type == FHF_T && o->type != FHF_T) { b1 -= d1->vdelta; b2 -= d2->vdelta; }
		wall(o->wtex, x1, y1, x2, y2, size, b1, b2, t1, t2, SQLIGHT(d1), SQLIGHT(d2));
	}
	{
		int b1 = o->ceil * 4, b2 = b1, t1 = s->ceil * 4, t2 = t1;
		if (o->type == CHF_T && s->type != CHF_T) { b1 += d1->vdelta; b2 += d2->vdelta; }
		else if (s->type == CHF_T && o->type != CHF_T) { t1 += d1->vdelta; t2 += d2->vdelta; }
		wall(o->utex, x1, y1, x2, y2, size, b1, b2, t1, t2, SQLIGHT(d1), SQLIGHT(d2));
	}
}

/* floor and ceiling triangles of one half of a corner square; la/lb/lc = the squares that light the three points */
static void corner_tris(const Sqr *h, int ax, int ay, int bx, int by, int cx, int cy, const Sqr *la, const Sqr *lb, const Sqr *lc) {
	SVECTOR a, b, c; uint16_t uv[3]; uint32_t l[3]; const TexInfo *t; int i, pass;
	int px[3], py[3];
	px[0] = ax; py[0] = ay; px[1] = bx; py[1] = by; px[2] = cx; py[2] = cy;
	a.vx = ax << 6; a.vz = ay << 6; b.vx = bx << 6; b.vz = by << 6; c.vx = cx << 6; c.vz = cy << 6;
	l[0] = SQLIGHT(la); l[1] = SQLIGHT(lb); l[2] = SQLIGHT(lc);
	for (pass = 0; pass < 2; pass++) {
		int tex = pass ? h->ctex : h->ftex, hh = pass ? h->ceil : h->floor, minx = ax < bx ? ax : bx, miny = ay < by ? ay : by;
		if (pass ? (vh > hh * 256 || !render_ceil) : (vh < hh * 256 || !render_floor)) continue;
		if (tex == 0) { sky_needed = 1; continue; }
		if (cx < minx) minx = cx;
		if (cy < miny) miny = cy;
		t = texof(tex);
		for (i = 0; i < 3; i++) {
			int u = 0, v = 0;
			if (t) { u = (((px[i] - minx) + (minx & ((1 << t->lw) - 1))) * 128) >> t->lw; v = (((py[i] - miny) + (miny & ((1 << t->lh) - 1))) * 128) >> t->lh; }
			if (u > 127) u = 127;
			if (v > 127) v = 127;
			uv[i] = UV(u, v);
		}
		a.vy = b.vy = c.vy = -hh * 64;
		wtri(&a, &b, &c, uv, t, l, OT_WORLD);
	}
}
static const Sqr solid_sqr = { .type = SOLID_T, .floor = 0, .ceil = 16, .wtex = 2, .ftex = 3, .ctex = 4, .utex = 2, .tl = 0x0a0a0a00 };
static inline const Sqr *sqat(int m, int x, int y) {
	int sz = ssz >> m;
	if ((unsigned)x >= (unsigned)sz || (unsigned)y >= (unsigned)sz) return &solid_sqr;
	return &wmip[m][y * sz + x];
}

/* Cube's issemi: does a higher-mip solid/semi-solid square really have open space along the given edge? */
static int issemi(int mip, int x, int y, int x1, int y1, int x2, int y2) {
	const Sqr *w;
	if (!(mip--)) return 1;
	x *= 2; y *= 2;
	w = sqat(mip, x + x1, y + y1);
	if (w->type == SEMISOLID_T) { if (issemi(mip, x + x1, y + y1, x1, y1, x2, y2)) return 1; }
	else if (w->type != CORNER_T && w->type != SOLID_T) return 1;
	w = sqat(mip, x + x2, y + y2);
	if (w->type == SEMISOLID_T) { if (issemi(mip, x + x2, y + y2, x1, y1, x2, y2)) return 1; }
	else if (w->type != CORNER_T && w->type != SOLID_T) return 1;
	return 0;
}

static void things_at(int m, int x, int y);
static int ntref;

/* ---------- water (rendercubes.cpp renderwater) ----------
 * Cube draws one see-through surface 0.3 units under the water level over every square whose floor is below it, its
 * corners bobbing on sin(x*y*0.1 + t/300ms) * 0.2 units.  Here each such square gets its own quad as it is walked:
 * after its walls and floor, before what stands on it - so it covers what is under water behind it and nearer
 * geometry still covers it. */
static int water_on, water_h, water_t1;          /* surface height (PS units, y up), wave phase */
static int water_ph(int x, int y) { return -(water_h - (isin((x * y * 65 + water_t1) & 4095) * 13 >> 12)); }
static uint32_t water_lt(int x, int y) {         /* a little shimmer in the light too */
	int v = 150 + (isin((x * 450 + y * 330 + water_t1 * 2) & 4095) * 20 >> 12);
	return v | (v << 8) | (v << 16);
}
static void water_square(int X, int Y, int size) {
	const TexInfo *t = &texinfo[255];
	int rep = 1 << t->lw, step = size < rep ? size : rep, x, y;
	semi_cmd = 0x02000000;
	for (y = Y; y < Y + size; y += step)
		for (x = X; x < X + size; x += step) {
			SVECTOR a, b, c, d; uint16_t uv[4]; uint32_t l[4];
			int u0 = ((x & (rep - 1)) * 128) >> t->lw, v0 = ((y & (rep - 1)) * 128) >> t->lh;
			int u1 = u0 + ((step * 128) >> t->lw) - 1, v1 = v0 + ((step * 128) >> t->lh) - 1;
			if (v1 > 127) v1 = 127;
			a.vx = x << 6; a.vz = y << 6; a.vy = water_ph(x, y);
			b.vx = (x + step) << 6; b.vz = y << 6; b.vy = water_ph(x + step, y);
			c.vx = x << 6; c.vz = (y + step) << 6; c.vy = water_ph(x, y + step);
			d.vx = (x + step) << 6; d.vz = (y + step) << 6; d.vy = water_ph(x + step, y + step);
			uv[0] = UV(u0, v0); uv[1] = UV(u1, v0); uv[2] = UV(u0, v1); uv[3] = UV(u1, v1);
			l[0] = water_lt(x, y); l[1] = water_lt(x + step, y); l[2] = water_lt(x, y + step); l[3] = water_lt(x + step, y + step);
			wquad(&a, &b, &c, &d, uv, t, l, OT_WORLD);
		}
	semi_cmd = 0;
}

/* scroll the liquid texture: upload it shifted by (du, dv) texels when that changes */
static void water_scroll(void) {
	int ms = VSync(-1) * 50 / 3, du, dv, y;
	const TexInfo *t = &texinfo[255];
	if (!water_px) return;
	du = (ms / 4000 * (128 >> t->lw) + (ms % 4000) * (128 >> t->lw) / 4000) & 127;
	dv = (ms / 4000 * (128 >> t->lh) + (ms % 4000) * (128 >> t->lh) / 4000) & 127;
	if (du == water_du && dv == water_dv) return;
	water_du = du; water_dv = dv;
	for (y = 0; y < 128; y++) {
		const uint8_t *src = water_px + ((y + dv) & 127) * 64; uint8_t *dst = water_buf + y * 64; int x;
		if (!(du & 1)) { int b = du >> 1; memcpy(dst, src + b, 64 - b); memcpy(dst + 64 - b, src, b); }
		else for (x = 0; x < 64; x++) {           /* odd shift: nibbles */
			int p0 = (du + 2 * x) & 127, p1 = (p0 + 1) & 127;
			int a = (src[p0 >> 1] >> ((p0 & 1) * 4)) & 15, b = (src[p1 >> 1] >> ((p1 & 1) * 4)) & 15;
			dst[x] = a | (b << 4);
		}
	}
	LoadImage(&water_vram, (const uint32_t *)water_buf);
}

/* draw one square of mip level m (the body of Cube's render_seg_new for a single square) */
static void render_square(int m, int xx, int yy, const Sqr *s) {
	int sz = ssz >> m, size = 1 << m, X = xx << m, Y = yy << m, normalwall = 1;
	const Sqr *t, *u, *v, *w, *z;
	if (xx > 0 && yy > 0 && xx < sz - 1 && yy < sz - 1) { t = s + 1; u = s + sz + 1; v = s + sz; w = s - sz; z = s - 1; }
	else { t = sqat(m, xx + 1, yy); u = sqat(m, xx + 1, yy + 1); v = sqat(m, xx, yy + 1); w = sqat(m, xx, yy - 1); z = sqat(m, xx - 1, yy); }
	if (s->type != SOLID_T) {
		if (ntref) things_at(m, xx, yy);
		/* (a height field square only when all its corners are under: with no depth buffer a surface over a bump would show) */
		if (water_on && (s->type == FHF_T ? s->floor * 4 - (s->vdelta < t->vdelta ? (s->vdelta < u->vdelta ? (s->vdelta < v->vdelta ? s->vdelta : v->vdelta) : (u->vdelta < v->vdelta ? u->vdelta : v->vdelta))
		                                                                          : (t->vdelta < u->vdelta ? (t->vdelta < v->vdelta ? t->vdelta : v->vdelta) : (u->vdelta < v->vdelta ? u->vdelta : v->vdelta)))
		                                      : s->floor * 4) < water_h / 16) water_square(X, Y, size);
		/* ceiling */
		if ((s->type == SPACE_T || s->type == FHF_T) && s->ceil * 256 >= vh && render_ceil) {
			if (s->ctex == 0) sky_needed = 1;
			else { int h = s->ceil * 4; flat(s->ctex, X, Y, size, h, h, h, h, s, t, u, v); }
		}
		if (s->type == CHF_T) {
			if (s->ctex == 0) sky_needed = 1;
			else { int h = s->ceil * 4; flat(s->ctex, X, Y, size, h + s->vdelta, h + t->vdelta, h + u->vdelta, h + v->vdelta, s, t, u, v); }
		}
		/* floor */
		if ((s->type == SPACE_T || s->type == CHF_T) && s->floor * 256 <= vh && render_floor) {
			if (s->ftex == 0) sky_needed = 1;          /* a floor of sky: Facing Cubes' abyss shows space below */
			else { int h = s->floor * 4; flat(s->ftex, X, Y, size, h, h, h, h, s, t, u, v); }
		}
		if (s->type == FHF_T) { int h = s->floor * 4; flat(s->ftex, X, Y, size, h - s->vdelta, h - t->vdelta, h - u->vdelta, h - v->vdelta, s, t, u, v); }
	}
	/*  w
	 * zSt
	 *  vu  */
	if (s->type == CORNER_T) {
		int topleft = 1, ax, ay, bx, by, px, py; const Sqr *h1 = 0, *h2 = 0, *wo, *ws; const Sqr *e1, *e2;
		if (z->type == SOLID_T) {
			if (w->type == SOLID_T) { wo = w; ws = h2 = s; ax = X + size; ay = Y; bx = X; by = Y + size; e1 = t; e2 = v; topleft = 0; }
			else if (v->type == SOLID_T) { wo = v; ws = h2 = s; ax = X; ay = Y; bx = X + size; by = Y + size; e1 = s; e2 = u; }
			else goto nocorner;
		} else if (t->type == SOLID_T) {
			if (w->type == SOLID_T) { wo = w; ws = h1 = s; ax = X + size; ay = Y + size; bx = X; by = Y; e1 = u; e2 = s; }
			else if (v->type == SOLID_T) { wo = v; ws = h1 = s; ax = X; ay = Y + size; bx = X + size; by = Y; e1 = v; e2 = t; topleft = 0; }
			else goto nocorner;
		} else {
			int wv = w->ceil - w->floor < v->ceil - v->floor;
			normalwall = 0;
			if (z->ceil - z->floor < t->ceil - t->floor) {
				if (wv) { wo = h1 = s; ws = h2 = v; ax = X + size; ay = Y; bx = X; by = Y + size; e1 = t; e2 = v; topleft = 0; }
				else { wo = h1 = s; ws = h2 = w; ax = X; ay = Y; bx = X + size; by = Y + size; e1 = s; e2 = u; }
			} else {
				if (wv) { wo = h2 = s; ws = h1 = v; ax = X + size; ay = Y + size; bx = X; by = Y; e1 = u; e2 = s; }
				else { wo = h2 = s; ws = h1 = w; ax = X; ay = Y + size; bx = X + size; by = Y; e1 = v; e2 = t; topleft = 0; }
			}
		}
		/* the diagonal is seen from the half that belongs to `ws`: test the viewer against that half's own corner */
		if (ws == h1) { px = X; py = topleft ? Y + size : Y; } else { px = X + size; py = topleft ? Y : Y + size; }
		{
			int ex = (bx - ax), ey = (by - ay);
			int sp = ex * (py - ay) - ey * (px - ax), sv = ex * ((vy >> 4) - (ay << 4)) - ey * ((vx >> 4) - (ax << 4));
			if ((sp > 0) == (sv > 0)) render_wall(wo, ws, ax, ay, bx, by, size, e1, e2);
		}
		if (topleft) {
			if (h1) corner_tris(h1, X + size, Y + size, X, Y + size, X, Y, u, v, s);
			if (h2) corner_tris(h2, X, Y, X + size, Y, X + size, Y + size, s, t, u);
		} else {
			if (h1) corner_tris(h1, X, Y, X + size, Y, X, Y + size, s, t, v);
			if (h2) corner_tris(h2, X + size, Y, X + size, Y + size, X, Y + size, t, u, v);
		}
	}
nocorner:
	if (normalwall) {
		int inner = xx != sz - 1 && yy != sz - 1, sol = s->type == SOLID_T;
		if (vx < (X << 8) && xx != 0 && yy != sz - 1 && z->type != SOLID_T && (!sol || z->type != CORNER_T)
		    && (z->type != SEMISOLID_T || issemi(m, xx - 1, yy, 1, 0, 1, 1)))
			render_wall(s, z, X, Y, X, Y + size, size, s, v);
		if (vx > ((X + size) << 8) && inner && t->type != SOLID_T && (!sol || t->type != CORNER_T)
		    && (t->type != SEMISOLID_T || issemi(m, xx + 1, yy, 0, 0, 0, 1)))
			render_wall(s, t, X + size, Y, X + size, Y + size, size, t, u);
		if (vy < (Y << 8) && yy != 0 && xx != sz - 1 && w->type != SOLID_T && (!sol || w->type != CORNER_T)
		    && (w->type != SEMISOLID_T || issemi(m, xx, yy - 1, 0, 1, 1, 1)))
			render_wall(s, w, X, Y, X + size, Y, size, s, t);
		if (vy > ((Y + size) << 8) && inner && v->type != SOLID_T && (!sol || v->type != CORNER_T)
		    && (v->type != SEMISOLID_T || issemi(m, xx, yy + 1, 0, 0, 1, 0)))
			render_wall(s, v, X, Y + size, X + size, Y + size, size, v, u);
	}
}

/* ---------- visibility: 360 degree angular occlusion buffer ----------
 * One bit per 1/4096 of a turn.  A square spans the half-open range [a1, a2) between its two silhouette corners; two
 * solid squares that share a corner compute the same angle for it, so a wall of them leaves no gap between their ranges. */
#define OCC_N 4096
static uint32_t occ[OCC_N / 32];
static int node_count;                         /* test rig: 1 = no occlusion culling, 2 = no merged squares, 4 = log visited squares, 8 = fixed view distance, 16 = no monsters / items */
void __attribute__((noipa, used)) dbg_log(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; __asm__ volatile(""); }

/* per-frame cache: the view angle of each map lattice point, for a window of AW x AW points around the viewer (the walk
   never goes further than the view distance; a point outside the window is just not cached) */
#define AW 167                       /* covers FAR_MAX + 8 cells either way, with slack */
#define AST 192                      /* row stride: x*192 is two shifts and an add */
static int16_t angc[AW * AST];
static uint8_t angs[AW * AST], ang_stamp;
static int ang_off;                  /* index of map lattice point (x,y) = y*AST + x - ang_off */
static int ang_ox, ang_oy;
_Static_assert(AW / 2 >= FAR_MAX + 8 + 2, "angle window too small");
static int angle_of(int f, int l) {            /* direction (forward f, right l) -> angle, 4096 = full turn, 0 = straight ahead, + = right */
	int af = f < 0 ? -f : f, al = l < 0 ? -l : l, a;
	if (af >= al) a = af ? atan_table[(al << 8) / af] : 0;
	else a = 1024 - atan_table[(af << 8) / al];
	if (f < 0) a = 2048 - a;
	return l < 0 ? -a : a;
}

static inline int occ_open(int lo, int hi) {   /* is any column in lo..hi still uncovered? */
	int wl, wh, i; uint32_t ml, mh;
	if (lo < 0) lo = 0;
	if (hi > OCC_N - 1) hi = OCC_N - 1;
	if (lo > hi) return 0;
	wl = lo >> 5; wh = hi >> 5; ml = ~0u << (lo & 31); mh = ~0u >> (31 - (hi & 31));
	if (wl == wh) return (~occ[wl] & ml & mh) != 0;
	if (~occ[wl] & ml) return 1;
	for (i = wl + 1; i < wh; i++) if (~occ[i]) return 1;
	return (~occ[wh] & mh) != 0;
}

static inline void occ_mark(int lo, int hi) {
	int wl, wh, i; uint32_t ml, mh;
	if (lo < 0) lo = 0;
	if (hi > OCC_N - 1) hi = OCC_N - 1;
	if (lo > hi) return;
	wl = lo >> 5; wh = hi >> 5; ml = ~0u << (lo & 31); mh = ~0u >> (31 - (hi & 31));
	if (wl == wh) { occ[wl] |= ml & mh; return; }
	occ[wl] |= ml;
	for (i = wl + 1; i < wh; i++) occ[i] = ~0u;
	occ[wh] |= mh;
}

/* The ground horizon: for each 1/1024 turn, the steepest slope up from the eye that nearer ground already hides
   ((height - eye) * 256 / distance, FX).  Anything farther that stays under it is skipped, with all it holds. */
#define HZ_N (OCC_N / 4)
static int32_t hz[HZ_N];
static int hz_on;
static void hz_setup(void) {
	int i;
	hz_on = world_htop[0] && sky_cur == 1 && !(dbg_flags & 1) && !(dbg_flags & 64);     /* the open (space) map: Cube's rooms gain nothing, at a 10% cost */
	for (i = 0; i < HZ_N; i++) hz[i] = -0x7fffffff;
}
static uint8_t tblk[32 * 32], tblk_stamp;
static int tblk_any(int m, int x, int y) {            /* does square (m,x,y) touch a block holding a thing this frame? */
	int b0x, b0y, b1x, b1y, bx, by;
	if (m >= 3) { b0x = x << (m - 3); b0y = y << (m - 3); b1x = b0x + (1 << (m - 3)); b1y = b0y + (1 << (m - 3)); }
	else { b0x = x >> (3 - m); b0y = y >> (3 - m); b1x = b0x + 1; b1y = b0y + 1; }
	if (b1x > 32) b1x = 32;
	if (b1y > 32) b1y = 32;
	for (by = b0y; by < b1y; by++) for (bx = b0x; bx < b1x; bx++) if (tblk[by * 32 + bx] == tblk_stamp) return 1;
	return 0;
}
static inline int hz_slope(int dh, int d) { return d < 64 ? (dh > 0 ? 0x7fffffff : -0x7fffffff) : (dh << 8) / d; }     /* (|dh| < 2^16: no overflow) */
static inline int hz_hidden(int lo, int hi, int slope) {     /* is every column of lo..hi (fine bins) under the horizon? */
	int k, k0 = (lo - 1) >> 2, k1 = (hi + 1) >> 2;
	if (k0 < 0) k0 = 0;
	if (k1 > HZ_N - 1) k1 = HZ_N - 1;
	for (k = k0; k <= k1; k++) if (hz[k] < slope) return 0;
	return 1;
}
static inline void hz_raise(int lo, int hi, int slope) {     /* ground covering lo..hi-1 (fine bins) hides up to `slope` */
	int k, k0 = (lo + 3) >> 2, k1 = (hi - 4) >> 2;
	if (k0 < 0) k0 = 0;
	if (k1 > HZ_N - 1) k1 = HZ_N - 1;
	for (k = k0; k <= k1; k++) if (hz[k] < slope) hz[k] = slope;
}

static void occ_setup(void) {
	/* widest horizontal half-angle of the view: the screen corners, tilted by the pitch */
	int cp = icos(pl.pitch), sp = isin(pl.pitch), den, half, i;
	if (sp < 0) sp = -sp;
	den = cp - (sp * (VIEW_B - VIEW_T) / 2) / PROJ_H;                 /* 4.12 */
	half = den <= 200 ? 2048 : angle_of(den, (4096 * (VIEW_R - VIEW_L) / 2) / PROJ_H) + 24;
	for (i = 0; i < OCC_N / 32; i++) occ[i] = 0;
	hz_setup();
	if (++ang_stamp == 0) { memset(angs, 0, sizeof(angs)); ang_stamp = 1; }
	ang_ox = (vx >> 8) - AW / 2; ang_oy = (vy >> 8) - AW / 2; ang_off = ang_oy * AST + ang_ox;
	if (half >= 2000) return;
	occ_mark(0, 2048 - half - 1); occ_mark(2048 + half + 1, OCC_N - 1);          /* everything outside the view counts as covered */
}

static inline int node_leaf_ok(const Sqr *s, int m, int dist) {      /* may this perfect-mip square be drawn as a single quad? */
	const TexInfo *t;
	if (m > MAX_LEAF) return 0;
	if (dist < (LOD_K * 256) << m) return 0;
	if (s->type == SOLID_T) { t = &texinfo[s->wtex]; return !t->ok || t->lw >= m; }
	t = &texinfo[s->ftex]; if (t->ok && (t->lw < m || t->lh < m)) return 0;
	if (s->ctex) { t = &texinfo[s->ctex]; if (t->ok && (t->lw < m || t->lh < m)) return 0; }
	return 1;
}

/* the front-to-back walk */
static void node(int m, int x, int y) {
	const Sqr *s = &wmip[m][y * (ssz >> m) + x];
	int x0 = x << (m + 8), y0 = y << (m + 8), sz = 256 << m, x1 = x0 + sz, y1 = y0 + sz;
	int inside = vx >= x0 && vx < x1 && vy >= y0 && vy < y1, dist = 0, lo = 0, hi = 0, wrap = 0, dnear = 0, dfar = 0;
	node_count++;
	if (!inside) {
		int ax, ay, bx, by, dx, dy, a1, a2, fax, fay, fbx, fby;
		/* nearest distance, and the two silhouette corners as seen from the viewer */
		if (vx < x0) { dx = x0 - vx; if (vy < y0) { dy = y0 - vy; ax = x1; ay = y0; bx = x0; by = y1; } else if (vy >= y1) { dy = vy - y1; ax = x0; ay = y0; bx = x1; by = y1; } else { dy = 0; ax = x0; ay = y0; bx = x0; by = y1; } }
		else if (vx >= x1) { dx = vx - x1; if (vy < y0) { dy = y0 - vy; ax = x0; ay = y0; bx = x1; by = y1; } else if (vy >= y1) { dy = vy - y1; ax = x1; ay = y0; bx = x0; by = y1; } else { dy = 0; ax = x1; ay = y0; bx = x1; by = y1; } }
		else { dx = 0; if (vy < y0) { dy = y0 - vy; ax = x0; ay = y0; bx = x1; by = y0; } else { dy = vy - y1; ax = x0; ay = y1; bx = x1; by = y1; } }
		dist = dx > dy ? dx + (dy >> 1) : dy + (dx >> 1);
		fax = ax; fay = ay; fbx = bx; fby = by;
		{
			int ia = (ay >> 8) * AST + (ax >> 8) - ang_off, ib = (by >> 8) * AST + (bx >> 8) - ang_off;
			if (m >= 3 || far_cells > FAR_MAX) {
				/* a fine square's parent passed the distance test, so its corners are within FAR_MAX + 8 cells and always in the
				   window; only the few coarse squares (and the rig's no-limit mode) need the check */
				int wax = (ax >> 8) - ang_ox, way = (ay >> 8) - ang_oy, wbx = (bx >> 8) - ang_ox, wby = (by >> 8) - ang_oy;
				if ((unsigned)wax >= AW || (unsigned)way >= AW) ia = -1;
				if ((unsigned)wbx >= AW || (unsigned)wby >= AW) ib = -1;
			}
			if (ia >= 0 && angs[ia] == ang_stamp) a1 = angc[ia];
			else { ax -= vx; ay -= vy; a1 = angle_of((ax * fwx + ay * fwy) >> 12, (ax * rtx + ay * rty) >> 12); if (ia >= 0) { angc[ia] = a1; angs[ia] = ang_stamp; } }
			if (ib >= 0 && angs[ib] == ang_stamp) a2 = angc[ib];
			else { bx -= vx; by -= vy; a2 = angle_of((bx * fwx + by * fwy) >> 12, (bx * rtx + by * rty) >> 12); if (ib >= 0) { angc[ib] = a2; angs[ib] = ang_stamp; } }
		}
		if (a1 > a2) { int tt = a1; a1 = a2; a2 = tt; }
		lo = a1 + 2048; hi = a2 + 2048;
		if (a2 - a1 > 2048) { wrap = 1; if (!(dbg_flags & 1) && !occ_open(hi - 1, OCC_N - 1) && !occ_open(0, lo + 1)) return; }
		else if (!(dbg_flags & 1) && !occ_open(lo - 1, hi + 1)) return;
		{   /* under the horizon of nearer ground? (true distances: at least the larger offset, at most a + b/2) */
			int fx = vx - x0 > x1 - vx ? vx - x0 : x1 - vx, fy = vy - y0 > y1 - vy ? vy - y0 : y1 - vy;
			if (fx < 0) fx = -fx;
			if (fy < 0) fy = -fy;
			dnear = dx > dy ? dx : dy; dfar = fx > fy ? fx + (fy >> 1) : fy + (fx >> 1);
			if (hz_on && !wrap) {
				/* the square's own drawing first; then, if something stands in it, 8 more units for that */
				int top = world_htop[m][y * (ssz >> m) + x] * 256 - vh;
				if (hz_hidden(lo, hi, top > 0 ? hz_slope(top, dnear) : hz_slope(top, dfar))) {
					if (!tblk_any(m, x, y)) return;
					top += 8 * 256;
					if (hz_hidden(lo, hi, top > 0 ? hz_slope(top, dnear) : hz_slope(top, dfar))) return;
				}
			}
		}
		if (dist > far_cells << 8) {
			/* past the view distance.  Whatever is in there is fogged to black, so one black wall across the square (between
			   its silhouette corners, floor to ceiling) stands in for it - otherwise the sky box would show through. */
			/* (in space - Facing Cubes - open sky is not hidden: the abyss gets nothing, far ground only a low strip, so
			   what is past the view fades into space instead of a black wall all round the horizon) */
			int top = s->ceil;
			if (sky_cur == 1 && s->ctex == 0) { if (s->ftex == 0) return; top = s->floor + 3; }
			if (s->type != SOLID_T && !wrap) {
				SVECTOR a, b, c, d; static const uint16_t uv0[4]; static const uint32_t black[4];
				a.vx = fax >> 2; a.vz = fay >> 2; b.vx = fbx >> 2; b.vz = fby >> 2; c.vx = a.vx; c.vz = a.vz; d.vx = b.vx; d.vz = b.vz;
				a.vy = b.vy = -top * 64; c.vy = d.vy = -s->floor * 64;
				wquad(&a, &b, &c, &d, uv0, (const TexInfo *)0, black, OT_WORLD);
			}
			return;
		}
	}
	/* a corner mip is never split (Cube: "don't ever split even if textures etc are different"): its diagonal only exists at that level */
	/* far away, a 2x2 of open sky-roofed ground whose heights differ by one unit (Facing Cubes' ramps) is drawn as one square
	   at the lower height: past 28 cells a unit is a pixel or two, and the ramps would otherwise be all single cells */
	int far_merge = m == 1 && s->defer && s->type == SPACE_T && s->ctex == 0 && dist > (28 << 8) && hz_on
	                && world_htop[1][y * (ssz >> 1) + x] - s->floor <= 1 && !(dbg_flags & 2);
	if (m > 0 && s->type != CORNER_T && !far_merge && (s->defer || inside || (dbg_flags & 2) || !node_leaf_ok(s, m, dist))) {
		int cx = x * 2, cy = y * 2, ix = vx >= ((cx + 1) << (m + 7)), iy = vy >= ((cy + 1) << (m + 7));
		node(m - 1, cx + ix, cy + iy);
		node(m - 1, cx + (ix ^ 1), cy + iy);
		node(m - 1, cx + ix, cy + (iy ^ 1));
		node(m - 1, cx + (ix ^ 1), cy + (iy ^ 1));
	} else {
		if (dbg_flags & 4) dbg_log(m | (s->type << 8) | (wrap << 16), x, y, (lo & 0xffff) | (hi << 16));
		skirt = ((dist + (384 << m)) >> 2) / 100 + 1;
		render_square(m, x, y, s);
		if (hz_on && !inside && !wrap && s->type == SPACE_T && s->ftex != 0) {     /* its ground now hides what is under it (plain floors only: a heightfield surface is not where its cell says) */
			int dh = s->floor * 256 - vh;
			hz_raise(lo, hi, dh > 0 ? hz_slope(dh, dfar) : hz_slope(dh, dnear));
			if (dbg_flags & 128) dbg_log(0x1000 | m, x, y, (lo & 0xffff) | (hi << 16));
		}
		if (m <= 2) {                            /* the automap remembers what you have seen up close */
			int cx = x << m, cy = y << m;
			SEEN_MARK(cx, cy);
			if (m == 2) { SEEN_MARK(cx + 2, cy); SEEN_MARK(cx, cy + 2); SEEN_MARK(cx + 2, cy + 2); }
		}
	}
	if (s->type == SOLID_T && !inside && !wrap) occ_mark(lo, hi - 1);
}

/* ---------- sky box ---------- */
static TexInfo skytex[6];
static int sky_ok;

static void sky_load(int which) {
	const uint8_t *p = (const uint8_t *)g_texbuf + 4; int i;
	sky_ok = 0; sky_cur = which;
	if (!cd_load(which ? "SKYSPACE.PAK" : "SKY.PAK", g_texbuf, sizeof(g_texbuf))) return;
	if (memcmp(g_texbuf, "SKY1", 4) != 0) return;
	for (i = 0; i < 6; i++, p += 8192 + 32) {
		int slot = 65 + i, page = slot >> 2, sub = slot & 3, X = 64 * (page - 10), Y = 256, cx = (slot % 40) * 16, cy = 240 + slot / 40;
		tex_load(p, X, Y, sub, cx, cy);
		tex_set(&skytex[i], X, Y, sub, cx, cy, 2, 2);
	}
	sky_ok = 1;
}
static void sky_init(void) { sky_load(0); }
/* tex_upload: a map with another sky box - load it now, while the buffer is free (the map comes in after) */
static void sky_for_map(void) { if (world_sky() != sky_cur) { DrawSync(0); sky_load(world_sky()); DrawSync(0); } }

/* faces in Cube's order: ft bk lf rt dn up. Corner = (x,y,z) sign triples: top-left, top-right, bottom-left, bottom-right as seen from inside */
static const int8_t sky_faces[6][4][3] = {
	{ { 1, -1, -1 }, { -1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 } },      /* ft  (-Z) */
	{ { -1, -1, 1 }, { 1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 } },          /* bk  (+Z) */
	{ { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, 1 }, { 1, 1, -1 } },          /* lf  (+X) */
	{ { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, -1 }, { -1, 1, 1 } },      /* rt  (-X) */
	{ { -1, 1, 1 }, { 1, 1, 1 }, { -1, 1, -1 }, { 1, 1, -1 } },          /* dn  (+Y) */
	{ { -1, -1, -1 }, { 1, -1, -1 }, { -1, -1, 1 }, { 1, -1, 1 } },      /* up  (-Y) */
};

static void draw_sky(void) {
	enum { N = 4, D = 1200 };                       /* 4x4 quads per face: the GPU maps textures affinely, so big quads would warp */
	int f, k, cx = pl.x >> 2, cy = -(pl.z >> 2), cz = pl.y >> 2, gx, gy;
	if (!sky_ok) return;
	fog_off = 1;
	for (f = 0; f < 6; f++) {
		const int8_t (*c)[3] = sky_faces[f];
		for (gy = 0; gy < N; gy++) for (gx = 0; gx < N; gx++) {
			SVECTOR v[4]; uint16_t uv[4]; static const int dx[4] = { 0, 1, 0, 1 }, dy[4] = { 0, 0, 1, 1 };
			int p[3], ax, zc;
			/* quick reject: the quad's centre direction must be within ~62 degrees of the view axis */
			for (ax = 0; ax < 3; ax++) p[ax] = D * (c[0][ax] * N * 2 + (c[1][ax] - c[0][ax]) * (2 * gx + 1) + (c[2][ax] - c[0][ax]) * (2 * gy + 1)) / (N * 2);
			zc = (cam.m[2][0] * p[0] + cam.m[2][1] * p[1] + cam.m[2][2] * p[2]) >> 12;
			if (zc <= 0) continue;
			{ int l2 = (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) >> 8, z2 = (zc * zc) >> 8; if (z2 * 9 < l2 * 2) continue; }
			for (k = 0; k < 4; k++) {
				int s = gx + dx[k], t = gy + dy[k], col = gx * 32 + dx[k] * 31, row = gy * 32 + dy[k] * 31;
				for (ax = 0; ax < 3; ax++) p[ax] = D * (c[0][ax] * N + (c[1][ax] - c[0][ax]) * s + (c[2][ax] - c[0][ax]) * t) / N;
				v[k].vx = cx + p[0]; v[k].vy = cy + p[1]; v[k].vz = cz + p[2];
				uv[k] = f >= 4 ? UV(127 - row, col) : UV(col, row);       /* dn / up are stored rotated a quarter turn */
			}
			{ static const uint32_t skyl[4] = { 0x808080, 0x808080, 0x808080, 0x808080 }; wquad(&v[0], &v[1], &v[2], &v[3], uv, &skytex[f], skyl, OT_SKY); }
		}
	}
	fog_off = 0;
}

/* ---------- 3D models (MD2 converted by tools/mkmodel.py) ---------- */
#define MDLBUF_SZ (88 * 2048)
static uint32_t mdlbuf[MDLBUF_SZ / 4];            /* model geometry + animation frames (from MODELS.PAK on the disc) */
enum { MDL_MON = 0, MDL_ITEM = 8, MDL_GUN = 17, MDL_FX = 22, MDL_FX_ICE = 23, MDL_FX_SLIME = 24, MDL_CARROT = 25, MDL_TELEPORTER = 26, MDL_PROP = 27, MDL_FLAG = 36, MDL_CLIPS = 37, MDL_PISTOL = 38, NUM_MODELS = 39 };
#define MAXMV 256
typedef struct {
	int nv, nf, nt0, nt1, ms;
	uint8_t clip[7][4];                  /* first, count, range lo, range hi */
	const uint8_t *tri0, *tri1; const int8_t *fr;
	const uint16_t *order;               /* hud guns: per stored frame, ntri0 triangle indices far to near (tools/gunorder.py) */
	TexInfo tex;
} Model;
static Model models[NUM_MODELS];
static int models_ok;
static const MATRIX *dc;                 /* camera matrix used by model_draw (world cam, or identity for the gun) */
static int mdl_scale = 256, tri_add;     /* model_draw overrides, reset by callers */

static void models_init(void) {
	/* MODELS.PAK is read into the (still unused) map-texture buffer; skins go to VRAM, the geometry is packed into mdlbuf */
	const uint8_t *p = (const uint8_t *)g_texbuf; int n, i; uint32_t used = 0;
	uint8_t *dst = (uint8_t *)mdlbuf;
	if (!cd_load("MODELS.PAK", g_texbuf, sizeof(g_texbuf))) return;
	if (memcmp(p, "MDL1", 4) != 0) return;
	n = p[4] | (p[5] << 8); if (n > NUM_MODELS) n = NUM_MODELS;
	for (i = 0; i < n; i++) {
		const uint8_t *q = p + (p[8 + i * 4] | (p[9 + i * 4] << 8) | (p[10 + i * 4] << 16) | ((uint32_t)p[11 + i * 4] << 24));
		/* skins: models 0-24 in quarter pages 40-64 (texture pages at x 0..447, y 256); the sky takes 65-70.  The rest go
		   to the free page at (960,256), with their CLUTs after the others on row 241 */
		Model *m = &models[i]; int slot = i < 25 ? 40 + i : i == MDL_PISTOL ? 71 : 72 + (i - 25), page = slot >> 2, sub = i < 25 || i == MDL_PISTOL ? slot & 3 : i - 25, X, Y, cx = (slot % 40) * 16, cy = 240 + slot / 40;
		const uint8_t *skin; int nv, nf, nt0, nt1, geom;
		nv = q[0] | (q[1] << 8); nf = q[2] | (q[3] << 8); nt0 = q[4] | (q[5] << 8); nt1 = q[6] | (q[7] << 8);
		m->nv = nv; m->nf = nf; m->nt0 = nt0; m->nt1 = nt1; m->ms = q[8] | (q[9] << 8);
		memcpy(m->clip, q + 12, 28);
		geom = (40 + 9 * (nt0 + nt1) + nf * nv * 3 + 3) & ~3;            /* everything before the (word-aligned) skin */
		if (q[10] & 1) geom += (nf * nt0 * 2 + 3) & ~3;                   /* draw orders */
		skin = q + geom;
		if (used + geom > sizeof(mdlbuf) || nv > MAXMV) return;
		memcpy(dst + used, q, geom);
		m->tri0 = dst + used + 40; m->tri1 = m->tri0 + 9 * nt0; m->fr = (const int8_t *)(m->tri1 + 9 * nt1);
		m->order = (q[10] & 1) ? (const uint16_t *)(dst + used + ((40 + 9 * (nt0 + nt1) + nf * nv * 3 + 3) & ~3)) : 0;
		used += geom;
		if (q[10] & 2) continue;                                        /* a map prop: its skin comes with the map's textures */
		X = i < 25 || i == MDL_PISTOL ? 64 * (page - 10) : 960; Y = 256;
		tex_load(skin, X, Y, sub, cx, cy);
		tex_set(&m->tex, X, Y, sub, cx, cy, 0, 0);
	}
	models_ok = (n == NUM_MODELS);
}

static void props_tex(void) { int k; for (k = 0; k < NUM_MAPMODELS; k++) models[MDL_PROP + k].tex = texinfo[240 + k]; }

/* ticks one full play of clip c takes (100 ms per original md2 frame = 6 ticks) */
static int model_period(int id, int c) { return (models[id].clip[c][2] | (models[id].clip[c][3] << 8)) * 6; }

/* Draw model `id` at world (wx,wy,wz) (FX units, wz = feet / origin height), rotated by yaw, into list `slot`.
 * clip + ft (ticks into it); period = ticks for one play (0 = natural speed); loop = wrap, else hold the last frame.
 * lodmode 0/1 = world model with fog and distance LOD, 2 = first-person gun (fixed light, two sided), 3 = glowing effect.
 * The model's triangles are depth sorted among themselves into NBK buckets and then spliced into the list nearest first,
 * so the model as a whole sits at this point of the front-to-back order. */
#define NBK 32
static void model_draw(int id, int wx, int wy, int wz, int yaw, int clip, int ft, int period, int loop, int lodmode, uint32_t light, int slot) {
	const Model *m = &models[id]; MATRIX R, M; VECTOR T;
	int first = m->clip[clip][0], count = m->clip[clip][1], i0 = 0, i1 = 0, t8 = 0, i, nt, zmin = 0x7fffffff, zmax = 0, zscale;
	int sx[MAXMV], sy[MAXMV], sz[MAXMV], znear = 19; const uint8_t *tri; const int8_t *fa, *fb;      /* near plane 0.3 units, as Cube's hud gun */
	uint32_t *bk[NBK], col;
	int s = isin(yaw), c = icos(yaw), tz;
	if (!models_ok || count == 0) return;
	if (period <= 0) period = model_period(id, clip);
	if (count > 1) {
		int q8;
		if (loop) { q8 = (ft % period) * 256 * count / period; i0 = q8 >> 8; i1 = i0 + 1 >= count ? 0 : i0 + 1; }
		else if (ft >= period) { i0 = i1 = count - 1; q8 = i0 << 8; }
		else { q8 = ft * 256 * (count - 1) / period; i0 = q8 >> 8; i1 = i0 + 1 >= count ? count - 1 : i0 + 1; }
		t8 = q8 & 255;
	}
	T.vx = wx >> 2; T.vy = -(wz >> 2); T.vz = wy >> 2;
	ApplyMatrixLV((MATRIX *)dc, &T, &T);
	T.vx += dc->t[0]; T.vy += dc->t[1]; T.vz += dc->t[2];
	tz = T.vz;
	if (tz < -400 || tz > (far_cells << 6) + 200 || iabs(T.vx) > tz + 500) return;
	if (lodmode >= 2) { tri = m->tri0; nt = m->nt0; }                       /* glowing effect: `light` is used as it is */
	else { int lo = tz > 256, f = fog_at(tz); tri = lo ? m->tri1 : m->tri0; nt = lo ? m->nt1 : m->nt0; if (f < 4) return; light = fog_col(light, f); }
	R.m[0][0] = c; R.m[0][1] = 0; R.m[0][2] = -s; R.m[1][0] = 0; R.m[1][1] = 4096; R.m[1][2] = 0; R.m[2][0] = s; R.m[2][1] = 0; R.m[2][2] = c;
	MulMatrix0((MATRIX *)dc, &R, &M);
	for (i = 0; i < 9; i++) ((int16_t *)M.m)[i] = (((int16_t *)M.m)[i] * m->ms >> 8) * mdl_scale >> 8;
	M.t[0] = T.vx; M.t[1] = T.vy; M.t[2] = T.vz;
	gte_SetRotMatrix(&M); gte_SetTransMatrix(&M);
	fa = m->fr + (first + i0) * m->nv * 3; fb = m->fr + (first + i1) * m->nv * 3;
	for (i = 0; i < m->nv; i++, fa += 3, fb += 3) {
		SVECTOR v; int16_t xy[2] __attribute__((aligned(4))); int z;
		if (t8) { v.vx = fa[0] + ((fb[0] - fa[0]) * t8 >> 8); v.vy = fa[1] + ((fb[1] - fa[1]) * t8 >> 8); v.vz = fa[2] + ((fb[2] - fa[2]) * t8 >> 8); }
		else { v.vx = fa[0]; v.vy = fa[1]; v.vz = fa[2]; }
		gte_ldv0(&v); gte_rtps(); gte_stsxy(xy); gte_stsz(&z);
		if (z <= PROJ_H / 2) {                          /* too close for the GTE's divider: project in software from the view-space position */
			int mac[3]; gte_stlvnl(mac);
			z = mac[2];
			if (z >= znear) { xy[0] = CENTERX + mac[0] * PROJ_H / z; xy[1] = CENTERY + mac[1] * PROJ_H / z; }
			else z = 0;
		}
		sx[i] = xy[0]; sy[i] = xy[1]; sz[i] = z;
		if (z < zmin) zmin = z;
		if (z > zmax) zmax = z;
	}
	gte_SetRotMatrix(&cam); gte_SetTransMatrix(&cam);
	if (zmin < znear) zmin = znear;
	zscale = (NBK << 16) / (zmax - zmin + 1);
	for (i = 0; i < NBK; i++) bk[i] = 0;
	col = light | (tri_add ? 0x26000000 : 0x24000000);
	for (i = 0; i < nt; i++, tri += 9) {
		int a = tri[0], b = tri[1], cc = tri[2], zz, bi, x0, y0, x1, y1, x2, y2; uint32_t *q;
		if (sz[a] < znear || sz[b] < znear || sz[cc] < znear) continue;
		x0 = sx[a]; y0 = sy[a]; x1 = sx[b]; y1 = sy[b]; x2 = sx[cc]; y2 = sy[cc];
		zz = sz[a] + sz[b] + sz[cc];
		bi = ((int)(((unsigned)zz * 21846u) >> 16) - zmin) * zscale >> 16;                          /* (zz / 3 - zmin) scaled to a bucket */
		if ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0) >= 0) continue;
		if ((x0 < VIEW_L && x1 < VIEW_L && x2 < VIEW_L) || (x0 >= VIEW_R && x1 >= VIEW_R && x2 >= VIEW_R)) continue;
		if ((y0 < VIEW_T && y1 < VIEW_T && y2 < VIEW_T) || (y0 >= VIEW_B && y1 >= VIEW_B && y2 >= VIEW_B)) continue;
		if ((unsigned)(x0 - CENTERX + 500) > 1000u || (unsigned)(x1 - CENTERX + 500) > 1000u || (unsigned)(x2 - CENTERX + 500) > 1000u ||
		    (unsigned)(y0 - CENTERY + 250) > 500u || (unsigned)(y1 - CENTERY + 250) > 500u || (unsigned)(y2 - CENTERY + 250) > 500u) continue;   /* too big for the GPU */
		if (nextpri + 32 > endpri) { dropped_polys++; break; }
		q = (uint32_t *)nextpri; nextpri += 32;
		q[1] = col;
		q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = (m->tex.uvbase + (tri[3] | (tri[4] << 8))) | m->tex.clutw;
		q[4] = (x1 & 0xffff) | (y1 << 16); q[5] = (m->tex.uvbase + (tri[5] | (tri[6] << 8))) | (m->tex.tpagew | (tri_add ? 1u << 21 : 0));
		q[6] = (x2 & 0xffff) | (y2 << 16); q[7] = m->tex.uvbase + (tri[7] | (tri[8] << 8));
		if (bi < 0) bi = 0;
		if (bi >= NBK) bi = NBK - 1;
		q[0] = (uint32_t)bk[bi]; bk[bi] = q; frame_polys++;
	}
	for (i = 0; i < NBK; i++) {                           /* nearest bucket first: the list is drawn in reverse */
		uint32_t *q = bk[i];
		while (q) { uint32_t *nx = (uint32_t *)q[0]; LINK(slot, q, 7); q = nx; }
	}
}

/* The first-person gun.  As in Cube it is the md2 drawn at the eye, so it reaches right up to (and behind) the near plane:
 * every triangle is clipped against that plane in view space and projected in software (the GTE cannot divide this close).
 * GUN_H gives the gun Cube's own 75 degree vertical field of view whatever the world view uses. */
#define GUN_H 120
#define GUN_NEAR 19                          /* Cube's 0.3 unit near plane for the hud gun */
#define GUN_MAXPRIM 420
static uint32_t gunbuf[2][GUN_MAXPRIM * 8];      /* the gun's primitives persist per frame buffer: an unchanged pose is just linked in again */
static uint32_t *gun_head[2], *gun_tail[2], gun_col[2]; static int gun_key[2] = { -1, -1 }, gun_n[2];
static void gun_draw(int id, int clip, int ft, int period, int loop, uint32_t light) {
	const Model *m = &models[id]; uint32_t *gp = gunbuf[dbi], *gend = gunbuf[dbi] + GUN_MAXPRIM * 8, *head = 0, *tail = 0; int key;
	int first = m->clip[clip][0], count = m->clip[clip][1], i0 = 0, i1 = 0, t8 = 0, i, zmax = GUN_NEAR + 1, zscale;
	int16_t px[MAXMV], py[MAXMV], pz[MAXMV]; const uint8_t *tri = m->tri0; const int8_t *fa, *fb;
	uint32_t *bk[NBK];
	if (!models_ok || count == 0) return;
	if (count > 1) {
		int q8;
		if (loop) { q8 = (ft % period) * 256 * count / period; i0 = q8 >> 8; i1 = i0 + 1 >= count ? 0 : i0 + 1; }
		else if (ft >= period) { i0 = i1 = count - 1; q8 = i0 << 8; }
		else { q8 = ft * 256 * (count - 1) / period; i0 = q8 >> 8; i1 = i0 + 1 >= count ? count - 1 : i0 + 1; }
		t8 = q8 & 255;
	}
	key = id | (i0 << 8) | (i1 << 16) | (t8 << 24);
	if (key == gun_key[dbi] && gun_head[dbi]) {           /* same pose as the last time this buffer was used */
		if (light != gun_col[dbi]) { gun_col[dbi] = light; for (i = 0; i < gun_n[dbi]; i++) gunbuf[dbi][i * 8 + 1] = 0x24000000 | light; }
		*gun_tail[dbi] = (7u << 24) | ot[OT_GUN]; ot[OT_GUN] = (uint32_t)gun_head[dbi] & 0xffffff;
		return;
	}
	fa = m->fr + (first + i0) * m->nv * 3; fb = m->fr + (first + i1) * m->nv * 3;
	for (i = 0; i < m->nv; i++, fa += 3, fb += 3) {       /* model space is already view space; only the stored scale applies */
		int x = fa[0] * 256 + (fb[0] - fa[0]) * t8, y = fa[1] * 256 + (fb[1] - fa[1]) * t8, z = fa[2] * 256 + (fb[2] - fa[2]) * t8;
		px[i] = (x * m->ms) >> 16; py[i] = (y * m->ms) >> 16; pz[i] = (z * m->ms) >> 16;
		if (pz[i] > zmax) zmax = pz[i];
	}
	zscale = (NBK << 16) / (zmax - GUN_NEAR + 1);
	for (i = 0; i < NBK; i++) bk[i] = 0;
	{ const uint16_t *ord = m->order ? m->order + (first + (t8 < 128 ? i0 : i1)) * m->nt0 : 0; int oi;
	for (oi = m->nt0 - 1; oi >= 0; oi--) {          /* the bucket is a stack: push near -> far so far is drawn first */
		CV p[4], o[4]; int n = 0, k, zz = 0, sx[4], sy[4], bi, out = 15;
		tri = m->tri0 + 9 * (ord ? ord[oi] : oi);
		for (k = 0; k < 3; k++) { int v = tri[k]; p[k].x = px[v]; p[k].y = py[v]; p[k].z = pz[v]; p[k].u = tri[3 + k * 2]; p[k].v = tri[4 + k * 2]; }
		if (p[0].z < GUN_NEAR && p[1].z < GUN_NEAR && p[2].z < GUN_NEAR) continue;
		/* back face cull in view space (Cube culls the hud gun too) */
		{ int ax = p[1].x - p[0].x, ay = p[1].y - p[0].y, az = p[1].z - p[0].z, bx = p[2].x - p[0].x, by = p[2].y - p[0].y, bz = p[2].z - p[0].z;
		  int nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
		  if (nx * p[0].x + ny * p[0].y + nz * p[0].z >= 0) continue; }
		if (p[0].z < GUN_NEAR || p[1].z < GUN_NEAR || p[2].z < GUN_NEAR) {
			for (k = 0; k < 3; k++) {                         /* clip against the near plane */
				const CV *a = &p[k], *b = &p[k == 2 ? 0 : k + 1];
				if (a->z >= GUN_NEAR) o[n++] = *a;
				if ((a->z >= GUN_NEAR) != (b->z >= GUN_NEAR)) {
					int t = ((a->z - GUN_NEAR) << 8) / (a->z - b->z);
					o[n].x = a->x + (((b->x - a->x) * t) >> 8); o[n].y = a->y + (((b->y - a->y) * t) >> 8); o[n].z = GUN_NEAR;
					o[n].u = a->u + (((b->u - a->u) * t) >> 8); o[n].v = a->v + (((b->v - a->v) * t) >> 8); n++;
				}
			}
		} else { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; n = 3; }
		for (k = 0; k < n; k++) {
			int c = 0;
			sx[k] = CENTERX + o[k].x * GUN_H / o[k].z; sy[k] = CENTERY + o[k].y * GUN_H / o[k].z; zz += o[k].z;
			if (sx[k] < VIEW_L) c |= 1;
			if (sx[k] >= VIEW_R) c |= 2;
			if (sy[k] < VIEW_T) c |= 4;
			if (sy[k] >= VIEW_B) c |= 8;
			out &= c;
			if (sx[k] < CENTERX - 500) sx[k] = CENTERX - 500;
			if (sx[k] > CENTERX + 500) sx[k] = CENTERX + 500;
			if (sy[k] < CENTERY - 250) sy[k] = CENTERY - 250;
			if (sy[k] > CENTERY + 250) sy[k] = CENTERY + 250;
		}
		if (out) continue;
		bi = ord ? 0 : (zz / n - GUN_NEAR) * zscale >> 16;     /* with a stored order everything goes in one bucket, in that order */
		if (bi < 0) bi = 0;
		if (bi >= NBK) bi = NBK - 1;
		for (k = 1; k + 1 < n; k++) {
			uint32_t *q = gp;
			if (gp + 8 > gend) break;
			gp += 8;
			q[1] = 0x24000000 | light;
			q[2] = (sx[0] & 0xffff) | (sy[0] << 16);         q[3] = (m->tex.uvbase + (o[0].u | (o[0].v << 8))) | m->tex.clutw;
			q[4] = (sx[k] & 0xffff) | (sy[k] << 16);         q[5] = (m->tex.uvbase + (o[k].u | (o[k].v << 8))) | m->tex.tpagew;
			q[6] = (sx[k + 1] & 0xffff) | (sy[k + 1] << 16); q[7] = m->tex.uvbase + (o[k + 1].u | (o[k + 1].v << 8));
			q[0] = (uint32_t)bk[bi]; bk[bi] = q; frame_polys++;
		}
	} }
	/* chain the buckets far -> near, remember the chain, and link it in */
	for (i = NBK - 1; i >= 0; i--) {
		uint32_t *q = bk[i];
		while (q) { uint32_t *nx = (uint32_t *)q[0]; if (tail) *tail = (7u << 24) | ((uint32_t)q & 0xffffff); else head = q; tail = q; q = nx; }
	}
	gun_head[dbi] = head; gun_tail[dbi] = tail; gun_key[dbi] = key; gun_col[dbi] = light; gun_n[dbi] = (int)(gp - gunbuf[dbi]) / 8;
	if (head) { *tail = (7u << 24) | ot[OT_GUN]; ot[OT_GUN] = (uint32_t)head & 0xffffff; }
}

/* ---------- things: monsters, items, projectiles, explosions ---------- */
/* Each thing is entered in every map cell its footprint touches; the world walk draws it when it reaches the first
 * (nearest) of those cells, which puts it in front of everything it stands on and behind anything nearer. */
enum { TH_MON, TH_ITEM, TH_PROJ, TH_FX, TH_TRIG };
#define MAXTREF 1024
#define TW 160                                    /* the thing index covers TW x TW cells around the viewer */
static uint16_t thead[TW * TW];                   /* per cell: 1 + index of its first tref */
static int th_ox, th_oy;
static struct { uint16_t thing, next, cell; } tref[MAXTREF];
#define TID_TRIG (MAXMON + MAXITEM + MAXPROJ + MAXFX)
#define TID_PROP (TID_TRIG + MAXTRIG)
#define TID_FLAG (TID_PROP + MAXMM)
static uint8_t tdrawn[TID_FLAG + 3];                  /* (TID_FLAG + 2: King of the Hill's marker) */

/* (tblk, tblk_stamp: this frame's 8x8-cell blocks that hold something - declared with the horizon) */
static void thing_add(int id, int x, int y, int rad) {
	int x0 = (x - rad) >> 8, x1 = (x + rad) >> 8, y0 = (y - rad) >> 8, y1 = (y + rad) >> 8, cx, cy;
	if (iabs(x - vx) > (far_cells << 8) || iabs(y - vy) > (far_cells << 8)) return;
	tdrawn[id] = 0;
	for (cy = y0 >> 3; cy <= y1 >> 3; cy++) for (cx = x0 >> 3; cx <= x1 >> 3; cx++) if ((unsigned)cx < 32 && (unsigned)cy < 32) tblk[cy * 32 + cx] = tblk_stamp;
	for (cy = y0; cy <= y1; cy++) for (cx = x0; cx <= x1; cx++) {
		int idx;
		if ((unsigned)cx >= (unsigned)ssz || (unsigned)cy >= (unsigned)ssz || ntref >= MAXTREF) continue;
		if ((unsigned)(cx - th_ox) >= TW || (unsigned)(cy - th_oy) >= TW) continue;
		idx = (cy - th_oy) * TW + (cx - th_ox);
		tref[ntref].thing = id; tref[ntref].next = thead[idx]; tref[ntref].cell = idx; thead[idx] = ++ntref;
	}
}

static void things_register(void) {
	int i;
	ntref = 0;
	if (++tblk_stamp == 0) { memset(tblk, 0, sizeof(tblk)); tblk_stamp = 1; }
	th_ox = (vx >> 8) - TW / 2; th_oy = (vy >> 8) - TW / 2;
	for (i = 0; i < nmons; i++) {
		Monster *m = &mons[i];
		if (!m->alive && game_ticks - m->animt0 > CORPSE_TICKS) continue;      /* corpses vanish after a few seconds */
		thing_add(i, m->x, m->y, m->radius + 64);
	}
	for (i = 0; i < nitems; i++) {
		Item *it = &items[i];
		if (it->taken || ((it->type < E_SHELLS || it->type > E_YELLOWARMOUR) && it->type != E_CLIPS)) continue;
		if (it->type == E_CLIPS && !models[MDL_CLIPS].tex.ok) continue;
		thing_add(MAXMON + i, it->x, it->y, 160);
	}
	for (i = 0; i < MAXPROJ; i++) if (projs[i].life) thing_add(MAXMON + MAXITEM + i, projs[i].x, projs[i].y, 100);
	for (i = 0; i < MAXFX; i++) if (fxs[i].life > 0) thing_add(MAXMON + MAXITEM + MAXPROJ + i, fxs[i].x, fxs[i].y, 400);
	for (i = 0; i < ntrigs; i++) {                 /* Cube shows teleporters, and carrots of type 0 and 2 until they fire */
		const Trig *t = &trigs[i];
		if (t->type == E_TELEPORT || (t->type == E_CARROT && t->spawned && (t->attr2 == 0 || t->attr2 == 2))) thing_add(TID_TRIG + i, t->x, t->y, 200);
	}
	for (i = 0; i < world_nmm; i++) {              /* props: drawn when a square near their foot is (their canopy may reach further) */
		const MapModel *mm = &world_mm[i];
		if (!models[MDL_PROP + mm->mdl].tex.ok) continue;
		thing_add(TID_PROP + i, mm->x, mm->y, mm->rad > 512 ? 512 : mm->rad);
	}
	for (i = 0; i < nflags; i++) if (models[MDL_FLAG].tex.ok) thing_add(TID_FLAG + i, flags[i].x, flags[i].y, 200);
	{ int hx, hy, hz, ho; if (models[MDL_FLAG].tex.ok && koth_hill(&hx, &hy, &hz, &ho)) thing_add(TID_FLAG + 2, hx, hy, 200); }
}

static void things_clear(void) { int i; for (i = 0; i < ntref; i++) thead[tref[i].cell] = 0; ntref = 0; }

static inline uint32_t light_at(int x, int y) {       /* Cube lights a model with the light of the square it stands on */
	int cx = x >> 8, cy = y >> 8;
	if ((unsigned)cx >= (unsigned)ssz || (unsigned)cy >= (unsigned)ssz) return 0x808080;
	return SQLIGHT(&wmip[0][cy * ssz + cx]);
}

static void thing_draw(int id) {
	dc = &cam;
	if (id < MAXMON) {
		Monster *m = &mons[id]; int feet = m->z - m->eye, clip, ft = game_ticks - m->animt0, period = 0, loop = 1;
		if (!m->alive) { clip = 4 + m->deadvar; loop = 0; }
		else if (m->anim == ANIM_ATTACK && ft < model_period(MDL_MON + m->type, 2)) { clip = 2; loop = 0; }
		else if (m->anim == ANIM_PAIN && ft < model_period(MDL_MON + m->type, 3)) { clip = 3; loop = 0; }
		else { clip = m->moving ? 1 : 0; ft = game_ticks + id * 13; }
		uint32_t lc = m->painflash ? 0xffffff : light_at(m->x, m->y);
		if (m->bot == 2 && game_mode == GM_DM) {         /* the other player, in deathmatch: green, not a bot's look */
			int r = lc & 255, g = (lc >> 8) & 255, b = (lc >> 16) & 255;
			r = r / 2; g = g * 3 / 2 + 30; b = b / 2;
			lc = r | ((g > 255 ? 255 : g) << 8) | (b << 16);
		} else if (m->bot && game_mode != GM_DM) {       /* team colour: red or blue */
			int r = lc & 255, g = (lc >> 8) & 255, b = (lc >> 16) & 255;
			if (m->team == 0) { r = r * 3 / 2 + 30; g = g / 2; b = b / 2; } else { b = b * 3 / 2 + 30; r = r / 2; g = g * 3 / 4; }
			lc = (r > 255 ? 255 : r) | (g << 8) | ((b > 255 ? 255 : b) << 16);
		}
		model_draw(MDL_MON + m->type, m->x, m->y, feet, m->yaw, clip, ft, period, loop, m->alive ? 0 : 1, lc, OT_WORLD);
	} else if (id < MAXMON + MAXITEM) {
		Item *it = &items[id - MAXMON];
		model_draw(it->type == E_CLIPS ? MDL_CLIPS : MDL_ITEM + it->type - E_SHELLS, it->x, it->y, it->z + 256 + ((isin(game_ticks * 108 + it->x) * 13) >> 12), game_ticks * 19, 0, 0, 1, 1, 0, light_at(it->x, it->y), OT_WORLD);
	} else if (id < MAXMON + MAXITEM + MAXPROJ) {
		Proj *p = &projs[id - MAXMON - MAXITEM];
		mdl_scale = p->gun == GUN_RL ? 70 : 90; tri_add = 1;
		model_draw(p->gun == GUN_ICEBALL ? MDL_FX_ICE : p->gun == GUN_SLIMEBALL ? MDL_FX_SLIME : MDL_FX, p->x, p->y, p->z, game_ticks * 61, 0, 0, 1, 1, 3, 0xffffff, OT_WORLD);
		mdl_scale = 256; tri_add = 0;
	} else if (id == TID_FLAG + 2) {                        /* the hill's marker: a flag in the holder's colour (white: nobody; flickering: contested) */
		int hx, hy, hz, ho; uint32_t col;
		if (!koth_hill(&hx, &hy, &hz, &ho)) return;
		col = ho == 0 ? 0x3030e0 : ho == 1 ? 0xe05030 : ho == 2 ? ((game_ticks >> 3) & 1 ? 0x3030e0 : 0xe05030) : 0xb0b0b0;
		model_draw(MDL_FLAG, hx, hy, hz + ((isin(game_ticks * 40) * 20) >> 12), game_ticks * 7, 0, 0, 1, 1, 0, col, OT_WORLD);
	} else if (id >= TID_FLAG) {                            /* a CTF flag: white cloth, tinted with its team's colour */
		const Flag *f = &flags[id - TID_FLAG]; int bob = f->state == 1 ? 0 : (isin(game_ticks * 40) * 20) >> 12;
		model_draw(MDL_FLAG, f->x + (f->state == 1 ? 90 : 0), f->y, f->z + bob, f->state == 1 ? 0 : game_ticks * 7, 0, 0, 1, 1, 0,
		           id - TID_FLAG == 0 ? 0x3030e0 : 0xe05030, OT_WORLD);
	} else if (id >= TID_PROP) {
		const MapModel *mm = &world_mm[id - TID_PROP];
		model_draw(MDL_PROP + mm->mdl, mm->x, mm->y, mm->z, mm->yaw, 0, 0, 1, 1, 0, light_at(mm->x, mm->y), OT_WORLD);
	} else if (id >= TID_TRIG) {
		const Trig *t = &trigs[id - TID_TRIG];
		int bob = (isin(game_ticks * 108 + t->x) * 13) >> 12, spin = game_ticks * (t->type == E_CARROT && t->attr2 == 2 ? 190 : 19);
		model_draw(t->type == E_TELEPORT ? MDL_TELEPORTER : MDL_CARROT, t->x, t->y, t->z + 256 + bob, spin, 0, 0, 1, 1, 0, light_at(t->x, t->y), OT_WORLD);
	} else {
		Fx *f = &fxs[id - MAXMON - MAXITEM - MAXPROJ]; int k = f->life * 18 > 255 ? 255 : f->life * 18;
		mdl_scale = 160 + (14 - f->life) * 40; tri_add = 1;
		model_draw(MDL_FX, f->x, f->y, f->z, game_ticks * 97, 0, 0, 1, 1, 3, k | ((k * 200 >> 8) << 8) | ((k * 130 >> 8) << 16), OT_WORLD);
		mdl_scale = 256; tri_add = 0;
	}
}

static void things_at(int m, int x, int y) {
	/* the square's cells, clipped once to the index window, in window coordinates */
	int size = 1 << m, x0 = (x << m) - th_ox, y0 = (y << m) - th_oy, x1 = x0 + size, y1 = y0 + size, cx, cy;
	if (!ntref) return;
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > TW) x1 = TW; if (y1 > TW) y1 = TW;
	for (cy = y0; cy < y1; cy++) {
		const uint16_t *h = &thead[cy * TW];
		for (cx = x0; cx < x1; cx++) {
			int r = h[cx];
			while (r) { int id = tref[r - 1].thing; if (!tdrawn[id]) { tdrawn[id] = 1; thing_draw(id); } r = tref[r - 1].next; }
		}
	}
}

/* ---------- menu graphics: UI.PAK (tools/mkui.py) ----------
 * Cube's font and the logo live in the texture page at (960,0) beside the debug font: logo u 0-127, font u 128-255,
 * both at v 64-191; their CLUTs at (960,192) and (976,192). */
static uint8_t glyph[94][4];
static int ui_ok;
/* portrait sprites (tools/mkface.py): heads 0-4 (health levels, dead), eyes 5-9 (centre, left, right, squint, blink),
   mouths 10-12 (cigar, grit, smirk), embers 13-14, smoke 15-17; offsets: eye x,y  mouth x,y  ember x,y */
enum { FS_HEAD = 0, FS_EYE = 5, FS_MOUTH = 10, FS_EMBER = 13, FS_SMOKE = 15, FS_N = 18 };
static uint8_t face_spr[FS_N][4], face_off[6];
static int face_ok, face_who, face_cig = 1;          /* face_cig: this face has the cigar (ember and smoke) */
static uint32_t ui_tpw, ui_font_clutw, ui_logo_clutw;
static uint32_t *ui_head, *ui_tail;

static void ui_load(void) {
	const uint8_t *p = (const uint8_t *)g_texbuf; RECT r;
	if (!cd_load("UI.PAK", g_texbuf, sizeof(g_texbuf)) || memcmp(p, "UI02", 4) != 0) return;
	p += 8;
	r.x = 992; r.y = 64; r.w = 32; r.h = 128; LoadImage(&r, (const uint32_t *)p); DrawSync(0);
	r.x = 960; r.y = 192; r.w = 16; r.h = 1; LoadImage(&r, (const uint32_t *)(p + 8192)); DrawSync(0);
	memcpy(glyph, p + 8192 + 32, sizeof(glyph));
	p += 8192 + 32 + sizeof(glyph);
	r.x = 960; r.y = 64; r.w = 32; r.h = 128; LoadImage(&r, (const uint32_t *)p); DrawSync(0);
	r.x = 976; r.y = 192; r.w = 16; r.h = 1; LoadImage(&r, (const uint32_t *)(p + 8192)); DrawSync(0);
	p += 8192 + 32;
	memcpy(ui_icon, p, 160);
	p += 160;
	/* the status bar portrait: 256x128 4bpp at (960,384) = texture page (960,256) v 128-255, CLUT at (544,241) */
	r.x = 960; r.y = 384; r.w = 64; r.h = 128; LoadImage(&r, (const uint32_t *)p); DrawSync(0);
	r.x = 544; r.y = 241; r.w = 16; r.h = 1; LoadImage(&r, (const uint32_t *)(p + 16384)); DrawSync(0);
	memcpy(face_spr, p + 16384 + 32, sizeof(face_spr));
	memcpy(face_off, p + 16384 + 32 + sizeof(face_spr), sizeof(face_off));
	face_ok = 1; face_who = 0; face_cig = 1;
	ui_tpw = (uint32_t)getTPage(0, 0, 960, 0) << 16;
	ui_font_clutw = (uint32_t)getClut(960, 192) << 16; ui_logo_clutw = (uint32_t)getClut(976, 192) << 16;
	ui_ok = 1;
}

/* UI primitives are chained in call order and drawn after everything else (see render_frame) */
static uint32_t *ui_alloc(int words) {
	uint32_t *p = (uint32_t *)nextpri;
	if (nextpri + (words + 1) * 4 > endpri) return 0;
	nextpri += (words + 1) * 4;
	p[0] = (uint32_t)words << 24;
	if (ui_tail) ui_tail[0] = (ui_tail[0] & 0xff000000) | ((uint32_t)p & 0xffffff); else ui_head = p;
	ui_tail = p;
	return p;
}

static void ui_quad(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int u0, int v0, int u1, int v1, uint32_t clutw, uint32_t col) {
	uint32_t *q = ui_alloc(9);
	if (!q) return;
	q[1] = 0x2c000000 | (col & 0xffffff);
	q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = (u0 | (v0 << 8)) | clutw;
	q[4] = (x1 & 0xffff) | (y1 << 16); q[5] = (u1 | (v0 << 8)) | ui_tpw;
	q[6] = (x2 & 0xffff) | (y2 << 16); q[7] = u0 | (v1 << 8);
	q[8] = (x3 & 0xffff) | (y3 << 16); q[9] = u1 | (v1 << 8);
}

int ui_text_width(const char *s, int size) {
	int w = 0;
	for (; *s; s++) {
		int c = (unsigned char)*s;
		if (c > 32 && c < 127) w += (glyph[c - 33][2] - glyph[c - 33][0]) * size / 16;
		else w += size / 2;                           /* Cube: a space is half the font height */
	}
	return w;
}

int ui_text(int x, int y, const char *s, int size, uint32_t col) {
	int x0 = x;
	if (!ui_ok) return 0;
	for (; *s; s++) {
		int c = (unsigned char)*s;
		if (c > 32 && c < 127) {
			const uint8_t *g = glyph[c - 33]; int w = (g[2] - g[0]) * size / 16, h = (g[3] - g[1]) * size / 16;
			ui_quad(x, y, x + w, y, x, y + h, x + w, y + h, 128 + g[0], 64 + g[1], 128 + g[2] - 1, 64 + g[3] - 1, ui_font_clutw, col);
			x += w;
		} else x += size / 2;
	}
	return x - x0;
}

void ui_logo(int cx, int cy, int size, int angle, uint32_t col) {
	int c = icos(angle) * size >> 13, s = isin(angle) * size >> 13;     /* half-size, rotated */
	if (!ui_ok) return;
	ui_quad(cx - c + s, cy - s - c, cx + c + s, cy + s - c, cx - c - s, cy - s + c, cx + c - s, cy + s + c, 0, 64, 127, 191, ui_logo_clutw, col);
}

void ui_rect(int x0, int y0, int x1, int y1, uint32_t col) {
	uint32_t *q = ui_alloc(3);
	if (!q) return;
	q[1] = 0x60000000 | (col & 0xffffff); q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = ((x1 - x0) & 0xffff) | ((y1 - y0) << 16);
}

void ui_dim(int x0, int y0, int x1, int y1, int amount) {
	uint32_t *q;
	if (amount <= 0) return;
	if (amount > 255) amount = 255;
	q = ui_alloc(1); if (!q) return;
	q[1] = 0xe1000000 | (2 << 5) | 0x200;              /* draw mode: semi-transparency = background - foreground */
	q = ui_alloc(3); if (!q) return;
	q[1] = 0x62000000 | amount | (amount << 8) | (amount << 16); q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = ((x1 - x0) & 0xffff) | ((y1 - y0) << 16);
}

/* ---------- HUD ---------- */
static void emit_rect2d(int x0, int y0, int x1, int y1, int r, int g, int b) {
	uint32_t *q = (uint32_t *)nextpri;
	if (nextpri + 24 > endpri) { dropped_polys++; return; }
	q[1] = 0x28000000 | r | (g << 8) | (b << 16);
	q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = (x1 & 0xffff) | (y0 << 16); q[4] = (x0 & 0xffff) | (y1 << 16); q[5] = (x1 & 0xffff) | (y1 << 16);
	LINK(OT_HUD, q, 5); nextpri += 24;
}

/* ---------- the portrait ----------
 * Layered sprites in the UI chain: head (by health), eye strip, mouth strip, cigar ember, smoke.  Timing runs on the
 * vblank counter so it is the same at any frame rate. */
#define FACE_X 8
#define FACE_Y (BAR_Y + 2)
static struct {
	int look, look_until, blink_until, next_blink, grin_until, last_kills, puff_at, puff_until;
	int sx[3], sy[3], st[3];            /* smoke: position (1/16 px, relative to the head), start time; st 0 = free */
	uint32_t seed;
} fc;

static int face_rnd(int n) { fc.seed = fc.seed * 1103515245u + 12345u; return (int)((fc.seed >> 16) % (uint32_t)n); }

static void face_sprite(int id, int x, int y, uint32_t col, int semi) {
	uint32_t *q = ui_alloc(4);
	const uint8_t *r = face_spr[id];
	if (!q) return;
	q[1] = (semi ? 0x66000000 : 0x64000000) | (col & 0xffffff);
	q[2] = (x & 0xffff) | (y << 16);
	q[3] = r[0] | ((128 + r[1]) << 8) | ((uint32_t)getClut(544, 241) << 16);
	q[4] = r[2] | (r[3] << 16);
}

/* the other face (WHO1.BIN: Giga Stacy) or back to the dude (WHO0.BIN): the same layout, loaded over the same VRAM */
void render_set_face(int who) {
	const uint8_t *p = (const uint8_t *)g_texbuf; RECT r;
	who &= 1;
	if (!face_ok || who == face_who) return;
	world_forget();
	if (!cd_load(who ? "WHO1.BIN" : "WHO0.BIN", g_texbuf, sizeof(g_texbuf))) return;
	DrawSync(0);
	r.x = 960; r.y = 384; r.w = 64; r.h = 128; LoadImage(&r, (const uint32_t *)p); DrawSync(0);
	r.x = 544; r.y = 241; r.w = 16; r.h = 1; LoadImage(&r, (const uint32_t *)(p + 16384)); DrawSync(0);
	memcpy(face_spr, p + 16384 + 32, sizeof(face_spr));
	memcpy(face_off, p + 16384 + 32 + sizeof(face_spr), sizeof(face_off));
	face_who = who; face_cig = face_spr[FS_EMBER][2] > 1;          /* (her ember sprites are 1x1 and empty) */
	fc.st[0] = fc.st[1] = fc.st[2] = 0;
}

static void face_texpage(int abr) {
	uint32_t *q = ui_alloc(1);
	if (q) q[1] = 0xe1000000 | getTPage(0, abr, 960, 256) | 0x200;
}

static void draw_face(void) {
	int now = VSync(-1), hp = pl.health, head, eye = 0, mouth = 0, i;
	uint32_t col = 0x808080;
	if (!face_ok) return;
	if (!fc.seed) { fc.seed = 0x1234567u + now; fc.next_blink = now + 120; fc.puff_at = now + 180; }
	face_texpage(0);
	if (pl.dead) { face_sprite(FS_HEAD + 4, FACE_X, FACE_Y, 0x707070, 0); fc.last_kills = pl.kills; return; }
	head = hp >= 75 ? 0 : hp >= 50 ? 1 : hp >= 25 ? 2 : 3;
	/* a kill gets a smirk */
	if (pl.kills < fc.last_kills) fc.last_kills = pl.kills;
	if (pl.kills > fc.last_kills) { fc.last_kills = pl.kills; fc.grin_until = now + 80; }
	/* eyes: glance left or right now and then, blink every few seconds */
	if (now >= fc.look_until) {
		if (fc.look) { fc.look = 0; fc.look_until = now + 60 + face_rnd(120); }
		else { fc.look = face_rnd(2) ? 1 : 2; fc.look_until = now + 35 + face_rnd(50); }
	}
	if (now >= fc.next_blink) { fc.blink_until = now + 7; fc.next_blink = now + 150 + face_rnd(200); }
	if (pl.hurt > 0) { eye = 3; mouth = 1; if (pl.hurt > 10) col = 0x7078b4; }       /* hit: squint, grit teeth, flush red */
	else {
		eye = now < fc.blink_until ? 4 : fc.look;
		if (now < fc.grin_until) mouth = 2;
		{ int sp = snd_speaking(); if (sp >= V_OHYEAH && sp < V_CAKE) mouth = (now >> 2) & 1 ? 2 : 0; }   /* talking: the jaw works */
	}
	face_sprite(FS_HEAD + head, FACE_X, FACE_Y, col, 0);
	if (eye) face_sprite(FS_EYE + eye, FACE_X + face_off[0], FACE_Y + face_off[1], col, 0);
	face_sprite(FS_MOUTH + mouth, FACE_X + face_off[2], FACE_Y + face_off[3], col, 0);
	/* the cigar: the ember glows up for a draw every few seconds, then lets out a puff of smoke */
	if (mouth == 0 && face_cig) {
		int glow = now >= fc.puff_at;
		if (glow && !fc.puff_until) fc.puff_until = now + 45;
		if (glow && now >= fc.puff_until) {
			for (i = 0; i < 3 && fc.st[i]; i++) ;
			if (i < 3) { fc.st[i] = now; fc.sx[i] = (face_off[4] + 1) << 4; fc.sy[i] = (face_off[5] - 3) << 4; }
			fc.puff_until = 0; fc.puff_at = now + 200 + face_rnd(240); glow = 0;
		}
		face_sprite(FS_EMBER + glow, FACE_X + face_off[4], FACE_Y + face_off[5], 0x808080, 0);
	}
	/* smoke rises and drifts back over his shoulder, growing, drawn 50/50 see-through; it stays inside the portrait box */
	face_texpage(0);
	for (i = 0; i < 3; i++) {
		int age, x, y;
		if (!fc.st[i]) continue;
		age = now - fc.st[i];
		x = (fc.sx[i] - age * 2 - (age < 20 ? 0 : (age - 20) * (age - 20) / 64)) >> 4; y = (fc.sy[i] - age * 5) >> 4;
		if (age > 110 || y < 1 || x < 0 || x > 38) { fc.st[i] = 0; continue; }
		face_sprite(FS_SMOKE + (age < 30 ? 0 : age < 70 ? 1 : 2), FACE_X + x, FACE_Y + y, age < 70 ? 0x687080 : 0x505860, 1);   /* tinted to a neutral grey */
	}
}

/* a face sprite stretched to twice its size (a textured four-point polygon) */
static void face_quad(int id, int x, int y) {
	uint32_t *q = ui_alloc(9); const uint8_t *r = face_spr[id]; int u = r[0], v = 128 + r[1], w = r[2], h = r[3];
	if (!q) return;
	q[1] = 0x2c808080;
	q[2] = (x & 0xffff) | (y << 16);                   q[3] = u | (v << 8) | ((uint32_t)getClut(544, 241) << 16);
	q[4] = ((x + w * 2) & 0xffff) | (y << 16);         q[5] = (u + w) | (v << 8) | ((uint32_t)(getTPage(0, 0, 960, 256) | 0x200) << 16);
	q[6] = (x & 0xffff) | ((y + h * 2) << 16);         q[7] = u | ((v + h) << 8);
	q[8] = ((x + w * 2) & 0xffff) | ((y + h * 2) << 16); q[9] = (u + w) | ((v + h) << 8);
}
void render_face_preview(int x, int y) {
	if (!face_ok) return;
	face_quad(FS_HEAD, x, y);
	face_quad(FS_EYE, x + face_off[0] * 2, y + face_off[1] * 2);
	face_quad(FS_MOUTH, x + face_off[2] * 2, y + face_off[3] * 2);
}

/* ---------- the automap (SELECT) ----------
   Doom's idea on Cube's grid: everything you have seen, from above, north up and centred on you.  2 pixels a cell;
   red = walls (solid cells beside open ground), brown = drops of more than a step, an arrow for you, the exit and the
   teleporters once seen.  Horizontal runs of the same colour are one rectangle each. */
static void map_tile(int slot, int x0, int y0, int w, int h, uint32_t rgb) {
	uint32_t *q = (uint32_t *)nextpri;
	if (nextpri + 16 > endpri) { dropped_polys++; return; }
	q[1] = 0x60000000 | rgb; q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = (w & 0xffff) | (h << 16);
	LINK(slot, q, 3); nextpri += 16;
}
static void map_tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb) {
	uint32_t *q = (uint32_t *)nextpri;
	if (nextpri + 20 > endpri) return;
	q[1] = 0x20000000 | rgb; q[2] = (x0 & 0xffff) | (y0 << 16); q[3] = (x1 & 0xffff) | (y1 << 16); q[4] = (x2 & 0xffff) | (y2 << 16);
	LINK(OT_HUD, q, 4); nextpri += 20;
}
#define MAP_WALL  0x2828d8           /* (b << 16 | g << 8 | r) */
#define MAP_STEP  0x204880
#define MAP_HW 82                    /* cells either side of you across, and up / down */
#define MAP_HH 48
#define MAXRUN 1400
typedef struct { int16_t cy, cx, n; uint8_t kind; } MapRun;     /* n cells of one kind, from cx going west */
static MapRun runs[MAXRUN]; static int nruns, run_key_x = -999, run_key_y, run_key_gen = -1, run_key_map = -1;

/* the map is drawn with x mirrored: in Cube's world, facing -y puts -x on your right, so a top-down view with +x to the
   right would be a mirror image of what you see */
static void map_build(void) {
	int pcx = pl.x >> 8, pcy = pl.y >> 8, cy;
	nruns = 0;
	for (cy = pcy - MAP_HH; cy <= pcy + MAP_HH; cy++) {
		int hi = pcx + MAP_HW, lo = pcx - MAP_HW, cx, rk = 0, rs = 0;
		const Sqr *row;
		if ((unsigned)cy >= (unsigned)ssz) continue;
		if (hi > ssz - 1) hi = ssz - 1; if (lo < 0) lo = 0;
		row = &wmip[0][cy * ssz];
		for (cx = hi; cx >= lo - 1; cx--) {             /* screen left to right = west-going */
			int k = 0;
			if (cx >= lo && SEEN(cx, cy)) {
				const Sqr *c = row + cx;
				if (c->type == SOLID_T) {
					if ((cx > 0 && c[-1].type != SOLID_T) || (cx < ssz - 1 && c[1].type != SOLID_T) ||
					    (cy > 0 && c[-ssz].type != SOLID_T) || (cy < ssz - 1 && c[ssz].type != SOLID_T)) k = 1;
				} else if ((cx < ssz - 1 && c[1].type != SOLID_T && iabs(c[1].floor - c->floor) > 2) ||
				           (cy < ssz - 1 && c[ssz].type != SOLID_T && iabs(c[ssz].floor - c->floor) > 2)) k = 2;
			}
			if (k != rk) {
				if (rk && nruns < MAXRUN) { MapRun *r = &runs[nruns++]; r->cy = cy; r->cx = rs; r->n = rs - cx; r->kind = rk; }
				rk = k; rs = cx;
			}
		}
	}
	run_key_x = pcx; run_key_y = pcy; run_key_gen = seen_gen; run_key_map = world_generation();
}

static void draw_automap(void) {
	int i, fx, fy, mx = 160, my = VIEW_B / 2;
	if ((pl.x >> 8) != run_key_x || (pl.y >> 8) != run_key_y || seen_gen != run_key_gen || world_generation() != run_key_map) map_build();
	map_tile(OT_SKY, 0, 0, SCREEN_XRES, VIEW_B, 0);                     /* black under everything */
	for (i = 0; i < nruns; i++) {
		const MapRun *r = &runs[i];
		int a = mx - ((((r->cx + 1) << 8) - pl.x) >> 7), b = a + r->n * 2, t = my + (((r->cy << 8) - pl.y) >> 7), h = 2;
		if (a < 0) a = 0; if (b > SCREEN_XRES) b = SCREEN_XRES; if (t < 0) { h += t; t = 0; } if (t + h > VIEW_B) h = VIEW_B - t;
		if (b > a && h > 0) map_tile(OT_WORLD, a, t, b - a, h, r->kind == 1 ? MAP_WALL : MAP_STEP);
	}
	for (i = 0; i < world_nents(); i++) {                               /* teleporters and the exit, once seen */
		const MapEnt *e = world_ent(i); uint32_t col;
		int ex, ey;
		if (e->type == E_TELEPORT) col = 0xd0c040;
		else if (e->type == E_CARROT && (e->attr4 & 1)) col = 0xd040d0;
		else continue;
		if ((unsigned)e->x >= (unsigned)ssz || (unsigned)e->y >= (unsigned)ssz || !SEEN(e->x, e->y)) continue;
		ex = mx - ((((e->x << 8) + 128) - pl.x) >> 7); ey = my + ((((e->y << 8) + 128) - pl.y) >> 7);
		if (ex > 2 && ex < SCREEN_XRES - 4 && ey > 2 && ey < VIEW_B - 4) map_tile(OT_HUD, ex - 3, ey - 3, 6, 6, col);
	}
	/* you: an arrow along the view direction (forward = -sin, cos of the yaw, x mirrored like the map) */
	fx = isin(pl.yaw); fy = icos(pl.yaw);
	map_tri(mx + (fx * 9 >> 12), my + (fy * 9 >> 12),
	        mx + ((-fx * 5 - fy * 5) >> 12), my + ((-fy * 5 + fx * 5) >> 12),
	        mx + ((-fx * 5 + fy * 5) >> 12), my + ((-fy * 5 - fx * 5) >> 12), 0x60f0f0);
	ui_text(6, 4, map_names[game_current_map()], 10, 0x406080);
	ui_text(6, VIEW_B - 14, "SELECT  BACK TO THE VIEW", 9, 0x283038);
}

static void draw_hud(void) {
	int cx = CENTERX, cy = CENTERY, g = pl.gun, i, full = menu_hud_visible();
	/* crosshair */
	int mapped = full && automap_on;
	if (full && !mapped && !menu_paused()) {
		emit_rect2d(cx - 6, cy - 1, cx - 2, cy + 1, 255, 255, 255);
		emit_rect2d(cx + 2, cy - 1, cx + 6, cy + 1, 255, 255, 255);
		emit_rect2d(cx - 1, cy - 6, cx + 1, cy - 2, 255, 255, 255);
		emit_rect2d(cx - 1, cy + 2, cx + 1, cy + 6, 255, 255, 255);
	}
	if (!pl.dead && !mapped) {
		int clip = 0, ft = 0, period = 1, loop = 1;
		if (pl.gunwait > 0 && gun_delay(g) > 1) { clip = 1; period = gun_delay(g); ft = period - pl.gunwait; loop = 0; }
		gun_draw(g == GUN_PISTOL ? MDL_PISTOL : MDL_GUN + g, clip, ft, period, loop, light_at(pl.x, pl.y));
	}
	if (!full) { emit_rect2d(0, VIEW_B, SCREEN_XRES, SCREEN_YRES, 0, 0, 0); return; }   /* title screen demo: just the gun, black below the view */
	if (pl.hurt) {
		int t = pl.hurt * 6; if (t > 200) t = 200;
		emit_rect2d(VIEW_L, VIEW_T, VIEW_R, VIEW_T + 6, t, 0, 0); emit_rect2d(VIEW_L, VIEW_B - 6, VIEW_R, VIEW_B, t, 0, 0);
		emit_rect2d(VIEW_L, VIEW_T, VIEW_L + 6, VIEW_B, t, 0, 0); emit_rect2d(VIEW_R - 6, VIEW_T, VIEW_R, VIEW_B, t, 0, 0);
	}
	/* status bar: portrait, weapon slots, then the background (added last = drawn first) */
	{
		draw_face();
		emit_rect2d(FACE_X - 2, BAR_Y + 2, FACE_X + 46, SCREEN_YRES, 30, 34, 44);          /* portrait box */
		emit_rect2d(FACE_X - 4, BAR_Y + 2, FACE_X + 48, SCREEN_YRES, 70, 70, 80);
		for (i = 0; i < 6; i++) {                                                            /* weapon slots: fist, pistol, SG, CG, RL, rifle */
			static const int8_t slot_gun[6] = { GUN_FIST, GUN_PISTOL, GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE };
			int gg = slot_gun[i], have = (gg == GUN_FIST) || pl.ammo[gg] > 0, sel = (gg == g);
			emit_rect2d(253 + i * 11 - 0, BAR_Y + 10, 253 + i * 11 + 8, BAR_Y + 18, sel ? 255 : have ? 150 : 50, sel ? 220 : have ? 150 : 50, sel ? 40 : have ? 160 : 55);
		}
		emit_rect2d(0, BAR_Y, SCREEN_XRES, BAR_Y + 2, 90, 90, 100);
		emit_rect2d(0, BAR_Y, SCREEN_XRES, SCREEN_YRES, 14, 14, 18);
	}
	FntPrint(0, "HEALTH %d  ARMOUR %d\n", pl.health, pl.armour);
	if (pl.gun == GUN_FIST) FntPrint(0, "%s\n", gun_name(pl.gun)); else FntPrint(0, "%s  AMMO %d\n", gun_name(pl.gun), pl.ammo[pl.gun]);
	FntPrint(0, "KILLS %d/%d  FPS %d\n", pl.kills, total_monsters, fps_now);
	if (game_msg_timer > 0 && (game_msg_for < 0 || !link_on || game_msg_for == link_me)) FntPrint(1, "%s\n", game_msg);
}

/* ---------- frame ---------- */
int render_view_cells(void) { return far_cells; }

void render_frame(void) {
	SVECTOR rot; VECTOR tpos; int t0;
	/* hold 30 fps: the last frame's build time (game logic + drawing, in scanlines; 526 = two vblanks) steers the view distance */
	if (dbg_flags & 32) far_cells = 250;       /* test rig: no distance limit, no fog */
	else if (dbg_flags & 8) far_cells = dbg_far;             /* test rig: a fixed view distance */
	else {
		int budget = FRAME_VBLANKS * 263;
		if (build_hb > budget || dropped_polys) far_cells = far_cells * (budget - 60) / (build_hb > budget ? build_hb : budget + 60);   /* missed: cut in proportion */
		else if (build_hb > budget - 70) far_cells -= 2;                                       /* close to the limit: back off */
		else if (build_hb < budget - 170 && far_cells < FAR_MAX) far_cells++;                  /* comfortable: see further */
		if (far_cells < FAR_MIN) far_cells = FAR_MIN;
	}
	{   /* under water: the view closes in (Cube shortens the fog to an eighth) */
		int w = world_water();
		underwater = w != NO_WATER && pl.z < w * 256 - 77 && menu_world_visible();
		if (underwater && far_cells > 24 && !(dbg_flags & 40)) far_cells = 24;
	}
	fog_setup();
	{
		int w = world_water();
		water_on = w != NO_WATER && texinfo[255].ok;
		water_h = w * 64 - 19;                     /* 0.3 units under the level */
		water_t1 = VSync(-1) * 50 / 3 * 2;         /* ms / 300 rad, in 1/4096 turns: x 4096 / (300 * 2 pi) ~ 2.17 */
		if (water_on) water_scroll();
	}
	frame_polys = 0; dropped_polys = 0; node_count = 0; sky_needed = 0;
	rot.vx = pl.pitch; rot.vy = pl.yaw; rot.vz = 0;
	RotMatrix(&rot, &cam);
	tpos.vx = -(pl.x >> 2); tpos.vy = (pl.z >> 2); tpos.vz = -(pl.y >> 2);
	ApplyMatrixLV(&cam, &tpos, &tpos);
	TransMatrix(&cam, &tpos);
	gte_SetRotMatrix(&cam);
	gte_SetTransMatrix(&cam);
	vx = pl.x; vy = pl.y; vh = pl.z; ssz = world_ssize();
	fwx = -isin(pl.yaw); fwy = icos(pl.yaw); rtx = icos(pl.yaw); rty = isin(pl.yaw);
	/* can any floor / ceiling be on screen at all? (the bottom / top edge of the view pointing below / above the horizon) */
	render_floor = ((VIEW_B - VIEW_T) / 2 * cam.m[1][1]) / PROJ_H + cam.m[2][1] > 0;
	render_ceil = -((VIEW_B - VIEW_T) / 2 * cam.m[1][1]) / PROJ_H + cam.m[2][1] < 0;
	ui_head = ui_tail = 0;
	if (!menu_world_visible()) goto ui;
	t0 = hb_now();
	draw_hud();
	diag_hud = hb_now() - t0; t0 = hb_now();
	if (automap_on && menu_hud_visible()) { draw_automap(); diag_world = hb_now() - t0; goto ui; }
#ifndef NO_WORLD
	occ_setup();
	if (!(dbg_flags & 16)) things_register();
	node(world_sfactor, 0, 0);
	things_clear();
#endif
	if (sky_needed) draw_sky();
	if (underwater) {                              /* murky: a see-through blue-green layer over the view, under the gun */
		uint32_t *q = (uint32_t *)nextpri;
		if (nextpri + 24 <= endpri) {
			q[1] = 0x62000000 | 12 | (54 << 8) | (70 << 16); q[2] = VIEW_L | (VIEW_T << 16); q[3] = (VIEW_R - VIEW_L) | ((VIEW_B - VIEW_T) << 16);
			LINK(OT_GUN, q, 3);
			q[5] = 0xe1000000 | getTPage(0, 0, 640, 0) | 0x200;          /* blend mode 0 (half and half), drawn just before it */
			LINK(OT_GUN, q + 4, 1); nextpri += 24;
		}
	}
	diag_world = hb_now() - t0;
	(void)diag_things;
	if (menu_hud_visible() && tex_got < 0) FntPrint(0, "TEXTURE FILE DID NOT LOAD\n");
	else if (menu_hud_visible() && tex_got < tex_want) FntPrint(0, "TEXTURES %d OF %d LOADED\n", tex_got, tex_want);
	if (menu_hud_visible()) FntPrint(0, "VIEW %d  POLYS %d  T %d\n", far_cells, frame_polys, build_hb * 100 / (FRAME_VBLANKS * 263));     /* T = % of the frame time used */
ui:
	menu_draw();
	if (ui_head) { ui_tail[0] = (ui_tail[0] & 0xff000000) | ot[OT_END]; ot[OT_END] = (uint32_t)ui_head & 0xffffff; }
	FntFlush(0); FntFlush(1);
	flip();
}
