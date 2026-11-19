/* Cube PSX - Cube (Wouter van Oortmerssen) single-player demake for the Sony PlayStation 1. */
#include "cube.h"
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxetc.h>

static char pad_buff[2][34];

static int axis(int v) {          /* 0..255 -> -127..127 with dead zone */
	v -= 128;
	if (v > -28 && v < 28) return 0;
	return (v > 0 ? v - 27 : v + 27) * 127 / 100;
}

int main(void) {
	int last_pad = 0, last_vs = 0;
	render_init();
	sound_init();
	music_init(); music_play(0);
	world_loading_cb = render_loading;
	/* BIOS pad and memory card drivers, in the order the BIOS wants them */
	InitPAD(&pad_buff[0][0], 34, &pad_buff[1][0], 34);
	InitCARD(1);
	StartPAD();
	StartCARD();
	_bu_init();
	ChangeClearPAD(0);
	menu_init();
	last_vs = VSync(-1);
	for (;;) {
		PADTYPE *pad = (PADTYPE *)&pad_buff[0][0];
		int held = 0, pressed, lx = 0, ly = 0, rx = 0, ry = 0, now, ticks, i, was_paused;
		if (pad->stat == 0) {
			held = (~pad->btn) & 0xffff;
			if (pad->type == PAD_ID_ANALOG || pad->type == PAD_ID_ANALOG_STICK) {
				lx = axis(pad->ls_x); ly = axis(pad->ls_y); rx = axis(pad->rs_x); ry = axis(pad->rs_y);
			}
		}
		pressed = held & ~last_pad; last_pad = held;
		now = VSync(-1); ticks = now - last_vs; last_vs = now;
		if (ticks < 1) ticks = 1;
		if (ticks > 4) ticks = 4;
		link_poll();
		was_paused = menu_paused();
		for (i = 0; i < ticks; i++) { menu_tick(held, i == 0 ? pressed : 0, lx, ly, rx, ry); }
		if (link_on) { if (was_paused || menu_paused()) link_frame(0, 0, 0, 0, 0, 0, ticks); else link_frame(held, pressed, lx, ly, rx, ry, ticks); }   /* a link game: the ticks both pads are in for */
		game_link_view(1);                       /* (the guest sees through human 1's eyes) */
		render_frame();
		sound_update();
		game_link_view(0);
		music_update(snd_duck());
	}
	return 0;
}
