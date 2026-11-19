/* xaenc: raw 16-bit stereo PCM at 37800 Hz -> PS1 CD-XA ADPCM sectors (4-bit, stereo, 37.8 kHz), one channel's worth.
   Output: 2336-byte Mode 2 Form 2 sectors (8-byte subheader, 18 sound groups of 128 bytes, 20 spare, 4 EDC = 0), the
   form mkpsxiso takes for type="xa" once the channels are interleaved (tools/mkmusic.py does that).

   usage: xaenc in.raw out.xas file_no channel_no
   A sound group = 8 sound units of 28 samples, alternating left / right; each unit picks one of the 4 XA filters and a
   shift (0..12) - all combinations are tried against the decoder's own running state and the least squared error wins.
   Build: gcc -O2 -o xaenc xaenc.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const int K0[4] = { 0, 60, 115, 98 }, K1[4] = { 0, 0, -52, -55 };

typedef struct { int old, older; } St;

static int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

/* encode 28 samples (stride 2 in src) with state st; writes 28 nibbles, returns the header byte */
static int enc_unit(const int16_t *src, int stride, St *st, uint8_t *nib) {
	int best_err_set = 0; long long best_err = 0; int best_f = 0, best_s = 0; uint8_t best_n[28]; St best_st = *st;
	int f, sh, i;
	for (f = 0; f < 4; f++)
		for (sh = 0; sh <= 12; sh++) {
			St s = *st; long long err = 0; uint8_t n[28];
			for (i = 0; i < 28; i++) {
				int x = src[i * stride], pred = (s.old * K0[f] + s.older * K1[f] + 32) >> 6;
				int r = x - pred, q, d;
				/* d = (q << 12) >> sh  ->  q = r << sh >> 12, rounded */
				q = ((r << sh) + (r >= 0 ? 2048 : -2048)) / 4096;
				if (q > 7) q = 7; if (q < -8) q = -8;
				d = clamp16(((q << 12) >> sh) + pred);
				err += (long long)(x - d) * (x - d);
				if (best_err_set && err >= best_err) break;
				n[i] = q & 15; s.older = s.old; s.old = d;
			}
			if (i < 28) continue;
			if (!best_err_set || err < best_err) { best_err_set = 1; best_err = err; best_f = f; best_s = sh; memcpy(best_n, n, 28); best_st = s; }
		}
	*st = best_st; memcpy(nib, best_n, 28);
	return (best_f << 4) | best_s;
}

int main(int argc, char **argv) {
	FILE *fi, *fo; long n; int16_t *pcm; int fileno, chan; long frames, nsec, k; St L = { 0, 0 }, R = { 0, 0 };
	if (argc < 5) { fprintf(stderr, "usage: xaenc in.raw out.xas file channel\n"); return 1; }
	fileno = atoi(argv[3]); chan = atoi(argv[4]);
	fi = fopen(argv[1], "rb"); if (!fi) { perror(argv[1]); return 1; }
	fseek(fi, 0, SEEK_END); n = ftell(fi); fseek(fi, 0, SEEK_SET);
	frames = n / 4; nsec = (frames + 2015) / 2016;
	pcm = calloc(nsec * 2016 * 2, 2); if (fread(pcm, 4, frames, fi) != (size_t)frames) return 1; fclose(fi);
	fo = fopen(argv[2], "wb"); if (!fo) { perror(argv[2]); return 1; }
	for (k = 0; k < nsec; k++) {
		uint8_t sec[2336]; int g;
		memset(sec, 0, sizeof(sec));
		sec[0] = sec[4] = fileno; sec[1] = sec[5] = chan;
		sec[2] = sec[6] = 0x64 | (k == nsec - 1 ? 0x81 : 0);     /* audio, form 2, real time (+ EOF / EOR on the last) */
		sec[3] = sec[7] = 0x01;                                   /* stereo, 37.8 kHz, 4 bit */
		for (g = 0; g < 18; g++) {
			uint8_t *grp = sec + 8 + g * 128; int u, i;
			const int16_t *base = pcm + (k * 2016 + g * 112) * 2;
			for (u = 0; u < 8; u++) {
				uint8_t nib[28]; int ch = u & 1, blk = u >> 1, h;
				h = enc_unit(base + blk * 28 * 2 + ch, 2, ch ? &R : &L, nib);
				if (u < 4) { grp[u] = grp[u + 4] = h; } else { grp[u + 4] = grp[u + 8] = h; }
				for (i = 0; i < 28; i++) {
					uint8_t *b = &grp[16 + i * 4 + (u >> 1)];
					if (u & 1) *b |= nib[i] << 4; else *b |= nib[i];
				}
			}
		}
		fwrite(sec, 1, 2336, fo);
	}
	fclose(fo);
	fprintf(stderr, "%s: %ld sectors (%.1f s)\n", argv[2], nsec, nsec * 2016 / 37800.0);
	return 0;
}
