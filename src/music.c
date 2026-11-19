/* Music: one CD-XA track per map (Doom style) and one for the menus, streamed from MUSIC.XA by the CD drive straight into
   the SPU's CD input - no CPU and no sound RAM.  MUSIC.XA holds 8 channels interleaved for 2x speed (tools/mkmusic.py);
   a track is a run of rows in one channel, a row being 8 sectors (one per channel), 18.75 rows a second.

   Looping is by the clock: the drive just plays on into the second of silence after the track, and we start it again.
   Loading a map takes the drive away; the track then picks up where it was. */
#include <stdint.h>
#include <psxcd.h>
#include <psxspu.h>
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include "cube.h"
#include "music_tab.h"

#define VB_PER_ROW_X5 16                    /* 60 vblanks / 18.75 rows = 3.2 vblanks a row */
static CdlFILE xa;
static int ok, cur = -1, want = -1, playing, start_vb, resume_row;

void music_init(void) {
	ok = CdSearchFile(&xa, "\\MUSIC.XA;1") != 0;
}

static void start(int t, int row) {
	CdlFILTER f; CdlLOC loc; uint8_t mode = CdlModeRT | CdlModeSF | CdlModeSpeed;
	if (row >= music_tracks[t].rows) row = 0;
	f.file = 1; f.chan = music_tracks[t].chan; f.pad = 0;
	CdControl(CdlSetfilter, &f, 0);
	CdControl(CdlSetmode, &mode, 0);
	CdIntToPos(CdPosToInt(&xa.pos) + (music_tracks[t].row + row) * 8, &loc);
	CdControl(CdlReadS, &loc, 0);
	cur = t; playing = 1; start_vb = VSync(-1) - row * VB_PER_ROW_X5 / 5;
}

void music_play(int track) { want = track; }
/* the XENO pack (catalog 15-22) shares the album: a track each, picked for the map's mood */
static const uint8_t music_xeno[8] = { 8, 12, 6, 3, 4, 11, 10, 14 };
void music_for_map(int cat) {
	want = cat >= 0 && cat < 15 ? music_of_cat[cat] : cat >= 15 && cat < CAT_N ? music_xeno[cat - 15] : MUSIC_TITLE;
}

/* cdload.c: the drive is about to read data - remember where the track was */
void music_cd_taken(void) {
	if (!playing) return;
	resume_row = (VSync(-1) - start_vb) * 5 / VB_PER_ROW_X5;
	playing = 0;
}

void music_update(int duck) {
	int v = opt_music * 0x4000 / 10 * duck >> 8;         /* 10 = half the CD input: the music sits under the effects */
	SpuSetCommonCDVolume(v, v);
	if (!ok) return;
	if (want != cur) {
		if (want < 0) { if (playing) CdControl(CdlPause, 0, 0); playing = 0; cur = -1; }
		else start(want, 0);
		return;
	}
	if (cur < 0) return;
	if (!playing) { start(cur, resume_row); return; }
	/* past the end (plus half the gap: the drive starts a little after the command) - round again */
	if (VSync(-1) - start_vb > music_tracks[cur].rows * VB_PER_ROW_X5 / 5 + 30) start(cur, 0);
}
