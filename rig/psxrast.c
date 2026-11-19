/* Minimal PS1 GPU software rasteriser for the test rig (host side, loaded with ctypes).
 * Handles what Cube PSX sends: GP0 polygons (flat / gouraud, textured or not, 3 or 4 points), tiles, sprites, fill, and the E1-E6 state commands.
 * Follows the hardware rules that matter for spotting bugs: 11-bit signed vertex coordinates, polygons wider than 1023 or
 * taller than 511 are dropped, top-left fill rule, quads are two triangles (0,1,2)+(1,2,3), affine texturing, texel 0 is
 * transparent, semi-transparency applies to textured pixels only when the texel's bit 15 is set. */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

uint16_t vram[512][1024];
static int da_x0, da_y0, da_x1 = 1023, da_y1 = 511, ofs_x, ofs_y;
static int tw_mx, tw_my, tw_ox, tw_oy;
static int g_tpage;
long stat_pixels, stat_dropped, stat_polys;
int last_drop[8];
int *rast_last_drop(void) { return last_drop; }

uint16_t *rast_vram(void) { return &vram[0][0]; }
void rast_reset_stats(void) { stat_pixels = stat_dropped = stat_polys = 0; }
long rast_stat(int i) { return i == 0 ? stat_pixels : i == 1 ? stat_dropped : stat_polys; }
void rast_set_env(int x0, int y0, int x1, int y1, int ox, int oy) { da_x0 = x0; da_y0 = y0; da_x1 = x1; da_y1 = y1; ofs_x = ox; ofs_y = oy; tw_mx = tw_my = tw_ox = tw_oy = 0; }

void rast_fill(int x, int y, int w, int h, int r, int g, int b) {
	int i, j; uint16_t c = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
	for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) vram[j & 511][i & 1023] = c;
}

typedef struct { int x, y, u, v, r, g, b; } Vtx;

static inline int sext11(int v) { v &= 0x7ff; return v & 0x400 ? v - 0x800 : v; }

static uint16_t texel(int u, int v, int tpage, int clut) {
	int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256, mode = (tpage >> 7) & 3;
	u = (u & ~(tw_mx * 8)) | ((tw_ox & tw_mx) * 8); v = (v & ~(tw_my * 8)) | ((tw_oy & tw_my) * 8);
	u &= 255; v &= 255;
	if (mode == 0) { int cx = (clut & 63) * 16, cy = (clut >> 6) & 511; uint16_t w = vram[(ty + v) & 511][(tx + (u >> 2)) & 1023]; return vram[cy][(cx + ((w >> ((u & 3) * 4)) & 15)) & 1023]; }
	if (mode == 1) { int cx = (clut & 63) * 16, cy = (clut >> 6) & 511; uint16_t w = vram[(ty + v) & 511][(tx + (u >> 1)) & 1023]; return vram[cy][(cx + ((w >> ((u & 1) * 8)) & 255)) & 1023]; }
	return vram[(ty + v) & 511][(tx + u) & 1023];
}

static inline int clamp5(int v) { return v < 0 ? 0 : v > 31 ? 31 : v; }

static void tri(const Vtx *a, const Vtx *b, const Vtx *c, int textured, int raw, int semi, int tpage, int clut) {
	int minx, maxx, miny, maxy, x, y; long long area;
	Vtx v0 = *a, v1 = *b, v2 = *c;
	minx = v0.x < v1.x ? v0.x : v1.x; if (v2.x < minx) minx = v2.x;
	maxx = v0.x > v1.x ? v0.x : v1.x; if (v2.x > maxx) maxx = v2.x;
	miny = v0.y < v1.y ? v0.y : v1.y; if (v2.y < miny) miny = v2.y;
	maxy = v0.y > v1.y ? v0.y : v1.y; if (v2.y > maxy) maxy = v2.y;
	if (maxx - minx > 1023 || maxy - miny > 511) { stat_dropped++; last_drop[0] = v0.x; last_drop[1] = v0.y; last_drop[2] = v1.x; last_drop[3] = v1.y; last_drop[4] = v2.x; last_drop[5] = v2.y; last_drop[6] = textured; last_drop[7] = tpage; return; }
	area = (long long)(v1.x - v0.x) * (v2.y - v0.y) - (long long)(v2.x - v0.x) * (v1.y - v0.y);
	if (area == 0) return;
	if (area < 0) { Vtx t = v1; v1 = v2; v2 = t; area = -area; }
	if (minx < da_x0) minx = da_x0;
	if (miny < da_y0) miny = da_y0;
	if (maxx > da_x1) maxx = da_x1;
	if (maxy > da_y1) maxy = da_y1;
	for (y = miny; y <= maxy; y++) for (x = minx; x <= maxx; x++) {
		long long w0 = (long long)(v1.x - x) * (v2.y - y) - (long long)(v2.x - x) * (v1.y - y);
		long long w1 = (long long)(v2.x - x) * (v0.y - y) - (long long)(v0.x - x) * (v2.y - y);
		long long w2 = (long long)(v0.x - x) * (v1.y - y) - (long long)(v1.x - x) * (v0.y - y);
		int e0x = v2.x - v1.x, e0y = v2.y - v1.y, e1x = v0.x - v2.x, e1y = v0.y - v2.y, e2x = v1.x - v0.x, e2y = v1.y - v0.y;
		int r, g, bl; uint16_t dst, out;
		if (w0 < 0 || w1 < 0 || w2 < 0) continue;
		/* top-left rule: pixels exactly on a right or bottom edge are not drawn */
		if (w0 == 0 && !((e0y < 0) || (e0y == 0 && e0x > 0))) continue;
		if (w1 == 0 && !((e1y < 0) || (e1y == 0 && e1x > 0))) continue;
		if (w2 == 0 && !((e2y < 0) || (e2y == 0 && e2x > 0))) continue;
		r = (int)((w0 * v0.r + w1 * v1.r + w2 * v2.r) / area); g = (int)((w0 * v0.g + w1 * v1.g + w2 * v2.g) / area); bl = (int)((w0 * v0.b + w1 * v1.b + w2 * v2.b) / area);
		dst = vram[y & 511][x & 1023];
		if (textured) {
			int u = (int)((w0 * v0.u + w1 * v1.u + w2 * v2.u) / area), v = (int)((w0 * v0.v + w1 * v1.v + w2 * v2.v) / area);
			uint16_t t = texel(u, v, tpage, clut);
			int tr = t & 31, tg = (t >> 5) & 31, tb = (t >> 10) & 31;
			if (t == 0) continue;
			if (!raw) { tr = clamp5(tr * r >> 7); tg = clamp5(tg * g >> 7); tb = clamp5(tb * bl >> 7); }
			if (semi && (t & 0x8000)) {
				int dr = dst & 31, dg = (dst >> 5) & 31, dbl = (dst >> 10) & 31, m = (tpage >> 5) & 3;
				if (m == 0) { tr = (dr + tr) >> 1; tg = (dg + tg) >> 1; tb = (dbl + tb) >> 1; }
				else if (m == 1) { tr = clamp5(dr + tr); tg = clamp5(dg + tg); tb = clamp5(dbl + tb); }
				else if (m == 2) { tr = clamp5(dr - tr); tg = clamp5(dg - tg); tb = clamp5(dbl - tb); }
				else { tr = clamp5(dr + (tr >> 2)); tg = clamp5(dg + (tg >> 2)); tb = clamp5(dbl + (tb >> 2)); }
			}
			out = tr | (tg << 5) | (tb << 10) | (t & 0x8000);
		} else {
			int tr = r >> 3, tg = g >> 3, tb = bl >> 3;
			if (semi) {
				int dr = dst & 31, dg = (dst >> 5) & 31, dbl = (dst >> 10) & 31, m = (g_tpage >> 5) & 3;
				if (m == 0) { tr = (dr + tr) >> 1; tg = (dg + tg) >> 1; tb = (dbl + tb) >> 1; }
				else if (m == 1) { tr = clamp5(dr + tr); tg = clamp5(dg + tg); tb = clamp5(dbl + tb); }
				else if (m == 2) { tr = clamp5(dr - tr); tg = clamp5(dg - tg); tb = clamp5(dbl - tb); }
				else { tr = clamp5(dr + (tr >> 2)); tg = clamp5(dg + (tg >> 2)); tb = clamp5(dbl + (tb >> 2)); }
			}
			out = tr | (tg << 5) | (tb << 10);
		}
		vram[y & 511][x & 1023] = out; stat_pixels++;
	}
}

/* run one primitive packet (the words after the tag); returns 0 if it met a command it does not know */
int rast_gp0(const uint32_t *w, int n) {
	int i = 0;
	while (i < n) {
		uint32_t c = w[i]; int cmd = c >> 24;
		if (cmd == 0x00) { i++; continue; }
		if (cmd == 0x02) { rast_fill(w[i + 1] & 0x3f0, (w[i + 1] >> 16) & 0x1ff, ((w[i + 2] & 0x3ff) + 15) & ~15, (w[i + 2] >> 16) & 0x1ff, c & 255, (c >> 8) & 255, (c >> 16) & 255); i += 3; continue; }
		if (cmd >= 0x20 && cmd <= 0x3f) {
			int gour = cmd & 0x10, quad = cmd & 0x08, tex = cmd & 0x04, semi = cmd & 0x02, raw = cmd & 0x01, nv = quad ? 4 : 3, k, tpage = g_tpage, clut = 0;
			Vtx v[4];
			for (k = 0; k < nv; k++) {
				uint32_t col = (k == 0 || gour) ? w[i++] : c;
				if (k == 0) col = c;
				v[k].r = col & 255; v[k].g = (col >> 8) & 255; v[k].b = (col >> 16) & 255;
				v[k].x = sext11(w[i] & 0xffff) + ofs_x; v[k].y = sext11(w[i] >> 16) + ofs_y; i++;
				if (tex) { v[k].u = w[i] & 255; v[k].v = (w[i] >> 8) & 255; if (k == 0) clut = w[i] >> 16; if (k == 1) { tpage = w[i] >> 16; g_tpage = (g_tpage & ~0x1ff) | (tpage & 0x1ff); } i++; } else { v[k].u = v[k].v = 0; }
			}
			stat_polys++;
			tri(&v[0], &v[1], &v[2], tex, raw, semi, tpage, clut);
			if (quad) tri(&v[1], &v[2], &v[3], tex, raw, semi, tpage, clut);
			continue;
		}
		if (cmd >= 0x64 && cmd <= 0x7f && (cmd & 0x04)) {            /* textured rectangles (sprites): tpage from E1, no flipping */
			int sz = (cmd >> 3) & 3, x = sext11(w[i + 1] & 0xffff) + ofs_x, y = sext11(w[i + 1] >> 16) + ofs_y, ww, hh, xx, yy;
			int semi = cmd & 2, raw = cmd & 1, u0 = w[i + 2] & 255, v0 = (w[i + 2] >> 8) & 255, clut = w[i + 2] >> 16;
			int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
			if (sz == 0) { ww = w[i + 3] & 0x3ff; hh = (w[i + 3] >> 16) & 0x1ff; i += 4; } else { ww = hh = sz == 1 ? 1 : sz == 2 ? 8 : 16; i += 3; }
			stat_polys++;
			for (yy = y; yy < y + hh; yy++) for (xx = x; xx < x + ww; xx++) {
				uint16_t t, dst; int tr, tg, tb;
				if (xx < da_x0 || xx > da_x1 || yy < da_y0 || yy > da_y1) continue;
				t = texel(u0 + xx - x, v0 + yy - y, g_tpage, clut);
				if (t == 0) continue;
				tr = t & 31; tg = (t >> 5) & 31; tb = (t >> 10) & 31;
				if (!raw) { tr = clamp5(tr * r >> 7); tg = clamp5(tg * g >> 7); tb = clamp5(tb * b >> 7); }
				dst = vram[yy & 511][xx & 1023];
				if (semi && (t & 0x8000)) {
					int dr = dst & 31, dg = (dst >> 5) & 31, dbl = (dst >> 10) & 31, m = (g_tpage >> 5) & 3;
					if (m == 0) { tr = (dr + tr) >> 1; tg = (dg + tg) >> 1; tb = (dbl + tb) >> 1; }
					else if (m == 1) { tr = clamp5(dr + tr); tg = clamp5(dg + tg); tb = clamp5(dbl + tb); }
					else if (m == 2) { tr = clamp5(dr - tr); tg = clamp5(dg - tg); tb = clamp5(dbl - tb); }
					else { tr = clamp5(dr + (tr >> 2)); tg = clamp5(dg + (tg >> 2)); tb = clamp5(dbl + (tb >> 2)); }
				}
				vram[yy & 511][xx & 1023] = tr | (tg << 5) | (tb << 10) | (t & 0x8000); stat_pixels++;
			}
			continue;
		}
		if (cmd >= 0x60 && cmd <= 0x7f && !(cmd & 0x04)) {           /* untextured rectangles (tiles) */
			int sz = (cmd >> 3) & 3, x = sext11(w[i + 1] & 0xffff) + ofs_x, y = sext11(w[i + 1] >> 16) + ofs_y, ww, hh, xx, yy, semi = cmd & 2;
			int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
			if (sz == 0) { ww = w[i + 2] & 0x3ff; hh = (w[i + 2] >> 16) & 0x1ff; i += 3; } else { ww = hh = sz == 1 ? 1 : sz == 2 ? 8 : 16; i += 2; }
			for (yy = y; yy < y + hh; yy++) for (xx = x; xx < x + ww; xx++) {
				int tr = r >> 3, tg = g >> 3, tb = b >> 3; uint16_t dst;
				if (xx < da_x0 || xx > da_x1 || yy < da_y0 || yy > da_y1) continue;
				dst = vram[yy & 511][xx & 1023];
				if (semi) {
					int dr = dst & 31, dg = (dst >> 5) & 31, dbl = (dst >> 10) & 31, m = (g_tpage >> 5) & 3;
					if (m == 0) { tr = (dr + tr) >> 1; tg = (dg + tg) >> 1; tb = (dbl + tb) >> 1; }
					else if (m == 1) { tr = clamp5(dr + tr); tg = clamp5(dg + tg); tb = clamp5(dbl + tb); }
					else if (m == 2) { tr = clamp5(dr - tr); tg = clamp5(dg - tg); tb = clamp5(dbl - tb); }
					else { tr = clamp5(dr + (tr >> 2)); tg = clamp5(dg + (tg >> 2)); tb = clamp5(dbl + (tb >> 2)); }
				}
				vram[yy & 511][xx & 1023] = tr | (tg << 5) | (tb << 10); stat_pixels++;
			}
			continue;
		}
		if (cmd == 0xe1) { g_tpage = c & 0x7ff; i++; continue; }
		if (cmd == 0xe2) { tw_mx = c & 31; tw_my = (c >> 5) & 31; tw_ox = (c >> 10) & 31; tw_oy = (c >> 15) & 31; i++; continue; }
		if (cmd == 0xe3) { da_x0 = c & 1023; da_y0 = (c >> 10) & 511; i++; continue; }
		if (cmd == 0xe4) { da_x1 = c & 1023; da_y1 = (c >> 10) & 511; i++; continue; }
		if (cmd == 0xe5) { ofs_x = sext11(c & 0x7ff); ofs_y = sext11((c >> 11) & 0x7ff); i++; continue; }
		if (cmd == 0xe6) { i++; continue; }
		return 0;
	}
	return 1;
}
