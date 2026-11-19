/* Cube PSX sound: Cube's sounds (SOUND.PAK, tools/mksound.py) on the PS1 sound chip, played the way Cube's sound.cpp
 * does it.  The whole bank lives in SPU RAM from start-up.  A positioned sound fades linearly to silence at 85 units
 * (vol -= dist * 3) and pans by its angle to the player; both are updated every frame while it plays, as Cube does
 * (updatevol).  At most 5 new sounds a frame (Cube's soundsatonce). */
#include "cube.h"
#include <psxgpu.h>
#include <psxspu.h>
#include <psxgte.h>
#include <psxapi.h>
#include <string.h>

#define SND_BASE 0x1010                 /* the SDK keeps the first 4 KB + 16 bytes of SPU RAM */
#define NVOICE 23                       /* effects; voice 23 is kept for speech */
#define SPEECH 23
#define MAXSND 96                       /* Cube 51 + voice 18, room to grow */

typedef struct { uint32_t off, size; uint16_t rate, pad; } SndEnt;
static uint16_t snd_addr[MAXSND], snd_pitch[MAXSND];
static uint32_t snd_len[MAXSND];       /* length in 1/60 s ticks, for voice bookkeeping */
static int nsnd, sound_ok;

static struct { int x, y, z, positioned, id, until; } voice[NVOICE];
static int next_voice, burst_frame, burst, speech_until, speech_id = -1;
/* ducking: while someone talks, everything else is turned down - the announcer to 20%, the dude to 60% - easing in and
   out over a few frames (a sudden volume step clicks) */
static int duck = 256;                  /* current effects level, 256 = full */
int snd_duck(void) { return duck; }

/* load one bank (SND1) and put it in SPU RAM at *spu, numbering its sounds on from nsnd */
static int voice_spu, voice_nsnd, voice_who;           /* where the voice lines start in SPU RAM and in the table; whose */
static int load_bank(const char *name, int *spu) {
	const uint8_t *p = (const uint8_t *)g_texbuf; const SndEnt *t; int i, n, size = 0;
	world_forget();
	if (!cd_load(name, g_texbuf, TEXBUF_SZ) || memcmp(p, "SND1", 4)) return 0;
	n = p[4] | (p[5] << 8); if (nsnd + n > MAXSND) n = MAXSND - nsnd;
	t = (const SndEnt *)(p + 8);
	for (i = 0; i < n; i++) if ((int)(t[i].off + t[i].size) > size) size = t[i].off + t[i].size;
	size = (size + 63) & ~63;
	if (*spu + size > 512 * 1024) return 0;
	for (i = 0; i < n; i++) {
		snd_addr[nsnd + i] = getSPUAddr(*spu + t[i].off);
		snd_pitch[nsnd + i] = getSPUSampleRate(t[i].rate);
		snd_len[nsnd + i] = t[i].size && t[i].rate ? t[i].size / 16 * 28 * 60 / t[i].rate + 2 : 0;
	}
	SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
	SpuSetTransferStartAddr(*spu);
	SpuWrite((const uint32_t *)(p + 8 + (p[4] | (p[5] << 8)) * 12), size);
	SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
	nsnd += n; *spu += size;
	return 1;
}

void sound_init(void) {
	int spu = SND_BASE;
	SpuInit();
	SpuSetCommonMasterVolume(0x3fff, 0x3fff);
	nsnd = 0;
	if (!load_bank("SOUND.PAK", &spu)) return;          /* Cube's sounds: numbers 0-50 */
	voice_spu = spu; voice_nsnd = nsnd;
	load_bank("VOICE.PAK", &spu);                        /* the voice lines: 51 on */
	sound_ok = 1;
}

/* Giga Chad's voice or Giga Stacy's: the same lines, loaded over each other (both are the same size, give or take) */
void snd_set_voice(int who) {
	int spu = voice_spu, v;
	if (!sound_ok || (who & 1) == voice_who) return;
	for (v = 0; v < NVOICE + 1; v++) SpuSetKey(0, 1 << v);           /* (quiet first: nothing may play what is being replaced) */
	nsnd = voice_nsnd; speech_until = 0;
	if (!load_bank(who & 1 ? "VOICE_F.PAK" : "VOICE.PAK", &spu)) { spu = voice_spu; nsnd = voice_nsnd; load_bank("VOICE.PAK", &spu); who = 0; }
	voice_who = who & 1;
}

/* Cube's updatechanvol: volume and pan for a sound at (x,y,z), or full and centred for the player's own */
static void voice_volume(int v) {
	int vol = 255, pan = 128, l, r, master = opt_sfx * 0x2400 / 10;     /* headroom: up to ~7 loud voices overlap in a chaingun burst */
	if (voice[v].positioned) {
		const Player *L = game_listener();
		int dx = voice[v].x - L->x, dy = voice[v].y - L->y, dz = voice[v].z - L->z;
		int dist = vlen3((dx >> 4), (dy >> 4), (dz >> 4)) >> 4;      /* units */
		vol -= dist * 3;
		if (vol < 0) vol = 0;
		if (dx || dy) {
			/* the sound's angle relative to where the player faces: right of the view = positive sine */
			int ang = yaw_to(dx, dy) - L->yaw;
			pan = 128 - (isin(ang & 4095) * 127 >> 12);
		}
	}
	vol = vol * duck >> 8;
	l = (pan < 128 ? 255 : (255 - pan) * 2); r = (pan > 128 ? 255 : pan * 2);
	if (l > 255) l = 255; if (r > 255) r = 255;
	SpuSetVoiceVolume(v, vol * l / 255 * master / 255, vol * r / 255 * master / 255);
}

static void play(int id, int positioned, int x, int y, int z) {
	int v, k, now = VSync(-1);
	if (!sound_ok || id < 0 || id >= nsnd || !snd_len[id] || !opt_sfx) return;
	if (burst_frame != now) { burst_frame = now; burst = 0; }
	if (++burst > 5) return;                                     /* Cube: no more than 5 sounds at once */
	if (positioned) {                                            /* out of earshot: don't take a voice */
		const Player *L = game_listener();
		int dx = (x - L->x) >> 8, dy = (y - L->y) >> 8;
		if (dx * dx + dy * dy > 85 * 85) return;
	}
	for (k = 0, v = next_voice; k < NVOICE; k++, v = (v + 1) % NVOICE) if (voice[v].until <= now) break;
	if (k == NVOICE) v = next_voice;                             /* all busy: take the oldest */
	next_voice = (v + 1) % NVOICE;
	SpuSetKey(0, 1 << v);
	voice[v].positioned = positioned; voice[v].x = x; voice[v].y = y; voice[v].z = z; voice[v].id = id;
	voice[v].until = now + snd_len[id];
	SPU_CH_FREQ(v) = snd_pitch[id];
	SPU_CH_ADDR(v) = snd_addr[id];
	SPU_CH_ADSR1(v) = 0x00ff;
	SPU_CH_ADSR2(v) = 0x0000;
	voice_volume(v);
	SpuSetKey(1, 1 << v);
}

/* in a link game the other console's human makes its own sounds out there, where it is */
void snd_play(int id) { if (game_sound_remote()) play(id, 1, pl.x, pl.y, pl.z); else play(id, 0, 0, 0, 0); }

/* speech: its own channel, centred, a little louder than the effects */
static int say_now(int id, int interrupt);
int snd_say(int id, int interrupt) { return game_sound_remote() ? 0 : say_now(id, interrupt); }
int snd_say_any(int id, int interrupt) { return say_now(id, interrupt); }     /* the announcer: for both consoles */
static int say_now(int id, int interrupt) {
	int now = VSync(-1), m = opt_speech * 0x3fff / 10;       /* speech at full volume: the effects keep headroom */
	if (!sound_ok || id < 0 || id >= nsnd || !snd_len[id] || !opt_speech) return 0;
	if (now < speech_until && !interrupt) return 0;
	SpuSetKey(0, 1 << SPEECH);
	SPU_CH_FREQ(SPEECH) = snd_pitch[id];
	SPU_CH_ADDR(SPEECH) = snd_addr[id];
	SPU_CH_ADSR1(SPEECH) = 0x00ff;
	SPU_CH_ADSR2(SPEECH) = 0x0000;
	SpuSetVoiceVolume(SPEECH, m, m);
	SpuSetKey(1, 1 << SPEECH);
	speech_until = now + snd_len[id]; speech_id = id;
	return 1;
}
int snd_speaking(void) { return VSync(-1) < speech_until ? speech_id : -1; }
void snd_play_at(int id, int x, int y, int z) { play(id, 1, x, y, z); }

/* once a frame: positioned sounds follow the player around (Cube's updatevol) */
void sound_update(void) {
	int v, now = VSync(-1), target = 256, old = duck;
	if (!sound_ok) return;
	if (now < speech_until) target = speech_id >= V_CAKE ? 51 : 154;
	if (duck > target) { duck -= 48; if (duck < target) duck = target; }
	else if (duck < target) { duck += 16; if (duck > target) duck = target; }
	for (v = 0; v < NVOICE; v++)
		if (voice[v].until > now && (voice[v].positioned || duck != old)) voice_volume(v);
}

void sound_stop_all(void) {
	int v;
	if (!sound_ok) return;
	SpuSetKey(0, 0xffffff);
	for (v = 0; v < NVOICE; v++) voice[v].until = 0;
	speech_until = 0;
}
