/* host stand-in for sound.c: counts what would have been played */
#include "cube.h"
int snd_count[64], snd_last_x, snd_last_y;
void sound_init(void) {}
void sound_update(void) {}
void sound_stop_all(void) {}
void snd_play(int id) { if (id >= 0 && id < 64) snd_count[id]++; }
void snd_play_at(int id, int x, int y, int z) { (void)z; snd_last_x = x; snd_last_y = y; snd_play(id); }
int say_count[128], say_last = -1, say_busy_until;
int snd_say(int id, int interrupt) {
	extern int game_ticks;
	if (!interrupt && game_ticks < say_busy_until) return 0;
	if (id >= 0 && id < 128) say_count[id]++;
	say_last = id; say_busy_until = game_ticks + (id >= V_GRUNT1 && id <= V_GRUNT3 ? 18 : 60);   /* a grunt 0.3 s, a line about 1 s */
	return 1;
}
int snd_say_any(int id, int interrupt) { return snd_say(id, interrupt); }
int snd_speaking(void) { extern int game_ticks; return game_ticks < say_busy_until ? say_last : -1; }

void music_cd_taken(void) {}
void music_play(int t) { (void)t; }
void music_for_map(int c) { (void)c; }
int snd_duck(void) { return 256; }

int render_view_cells(void) { return 72; }
