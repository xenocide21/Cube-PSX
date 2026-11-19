/* PSLink - two PlayStations joined by a link cable, in one window.
 *
 * Two copies of the PCSX-ReARMed libretro core (interpreter build, with the serial port SIO1 emulated) run side by
 * side; whatever one console sends down the link cable, the other receives.  Controller 1 plays console 1, controller
 * 2 plays console 2, and each console has a keyboard layout too (pslink.ini changes them).
 *
 *   pslink [disc.cue] [disc-for-console-2.cue]      (one disc: both consoles play it)
 *   --frames N  --script1 F  --script2 F  --shot PREFIX   (testing: run N frames headless, scripted pads, shots)
 *
 * PCSX-ReARMed is GPL v2; so is PSLink. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <SDL.h>
#include "libretro.h"
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_(p) mkdir(p, 0755)
#endif

#define VERSION "1.0"
#define RETRO_DEVICE_PSE_ANALOG RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)

/* ---------------- one console ---------------- */
typedef struct {
	void *lib;
	void (*init)(void), (*deinit)(void), (*run)(void), (*reset)(void);
	void (*set_env)(retro_environment_t), (*set_video)(retro_video_refresh_t), (*set_audio)(retro_audio_sample_t);
	void (*set_audio_batch)(retro_audio_sample_batch_t), (*set_poll)(retro_input_poll_t), (*set_state)(retro_input_state_t);
	bool (*load_game)(const struct retro_game_info *); void (*unload_game)(void);
	void (*get_av)(struct retro_system_av_info *); void (*set_port)(unsigned, unsigned);
	void (*sio_set_tx)(void (*)(unsigned char)); void (*sio_rx)(const unsigned char *, int); void (*sio_connect)(int);
	int pixfmt;
	SDL_Texture *tex; int tex_fmt;
	unsigned w, h;
	int16_t audio[16384]; int naudio;          /* stereo frames waiting to be mixed */
	unsigned char out[65536]; int nout; long sent, got;
	uint32_t buttons; int16_t axis[4];         /* the pad: libretro joypad bits; left x, left y, right x, right y */
	char savedir[512];
	FILE *script; char sline[256]; int snext; uint32_t sbuttons;
} Console;
static Console con[2];
static char base_dir[512], sys_dir[512], log_path[512];
static FILE *log_file;
static int use_hle = 1, headless, frames_max = -1, frame_no, paused, linked = 1, audio_mode, swap_pads, layout;
static const char *shot_prefix;
int headless_no_video(void);
static SDL_Window *win; static SDL_Renderer *ren; static SDL_AudioDeviceID adev;

static void say(const char *f, ...) {
	va_list a;
	va_start(a, f); vfprintf(stderr, f, a); va_end(a);
	if (log_file) { va_start(a, f); vfprintf(log_file, f, a); va_end(a); fflush(log_file); }
}
static void fail(const char *f, ...) {
	char b[1024]; va_list a;
	va_start(a, f); vsnprintf(b, sizeof(b), f, a); va_end(a);
	say("%s\n", b);
	if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PSLink", b, win);
	exit(1);
}

/* ---------------- libretro callbacks, one set per console ---------------- */
static void core_log(enum retro_log_level l, const char *f, ...) {
	char b[1024]; va_list a;
	if (l < RETRO_LOG_WARN && !getenv("PSLINK_VERBOSE")) return;
	va_start(a, f); vsnprintf(b, sizeof(b), f, a); va_end(a);
	say("core: %s", b);
}
static bool env_for(int c, unsigned cmd, void *data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = core_log; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: con[c].pixfmt = *(const enum retro_pixel_format *)data; return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sys_dir; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = con[c].savedir; return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data; v->value = NULL;
		if (!strcmp(v->key, "pcsx_rearmed_bios")) v->value = use_hle ? "HLE" : "auto";
		else if (!strcmp(v->key, "pcsx_rearmed_show_bios_bootlogo")) v->value = "disabled";
		return v->value != NULL; }
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
	default: return false;
	}
}
static void video_for(int c, const void *d, unsigned w, unsigned h, size_t pitch) {
	Console *k = &con[c]; SDL_Rect r;
	if (!d || headless_no_video()) return;
	{
		int fmt = k->pixfmt == RETRO_PIXEL_FORMAT_XRGB8888 ? SDL_PIXELFORMAT_ARGB8888 : k->pixfmt == RETRO_PIXEL_FORMAT_RGB565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB1555;
		if (!k->tex || k->tex_fmt != fmt) {
			if (k->tex) SDL_DestroyTexture(k->tex);
			k->tex = SDL_CreateTexture(ren, fmt, SDL_TEXTUREACCESS_STREAMING, 1024, 512); k->tex_fmt = fmt;
		}
	}
	if (w > 1024) w = 1024;
	if (h > 512) h = 512;
	r.x = 0; r.y = 0; r.w = w; r.h = h;
	SDL_UpdateTexture(k->tex, &r, d, (int)pitch);
	k->w = w; k->h = h;
}
static void audio_for(int c, const int16_t *d, size_t n) {
	Console *k = &con[c];
	if (k->naudio + (int)n > 8192) n = 8192 - k->naudio;      /* (behind: drop) */
	memcpy(k->audio + k->naudio * 2, d, n * 4); k->naudio += (int)n;
}
static int16_t input_for(int c, unsigned port, unsigned dev, unsigned idx, unsigned id) {
	Console *k = &con[c];
	if (port != 0) return 0;
	if ((dev & 0xff) == RETRO_DEVICE_JOYPAD) return id == RETRO_DEVICE_ID_JOYPAD_MASK ? (int16_t)k->buttons : (k->buttons >> id) & 1;
	if ((dev & 0xff) == RETRO_DEVICE_ANALOG && idx < 2 && id < 2) return k->axis[idx * 2 + id];
	return 0;
}
static void tx_for(int c, unsigned char b) { Console *k = &con[c]; if (k->nout < (int)sizeof(k->out)) k->out[k->nout++] = b; k->sent++; }
int headless_no_video(void) { return ren == NULL; }

#define CALLBACKS(n) \
	static bool env##n(unsigned cmd, void *d) { return env_for(n, cmd, d); } \
	static void video##n(const void *d, unsigned w, unsigned h, size_t p) { video_for(n, d, w, h, p); } \
	static void sample##n(int16_t l, int16_t r) { int16_t s[2] = { l, r }; audio_for(n, s, 1); } \
	static size_t batch##n(const int16_t *d, size_t f) { audio_for(n, d, f); return f; } \
	static void poll##n(void) {} \
	static int16_t state##n(unsigned p, unsigned d, unsigned i, unsigned id) { return input_for(n, p, d, i, id); } \
	static void tx##n(unsigned char b) { tx_for(n, b); }
CALLBACKS(0)
CALLBACKS(1)

/* ---------------- loading the cores ---------------- */
static void *sym(Console *k, const char *name) {
#ifdef _WIN32
	void *p = (void *)GetProcAddress((HMODULE)k->lib, name);
#else
	void *p = dlsym(k->lib, name);
#endif
	if (!p) fail("The emulator core is missing %s - is it the PSLink core?", name);
	return p;
}
static void core_open(int c) {
	Console *k = &con[c]; char path[600];
#ifdef _WIN32
	/* Windows loads a DLL once per name: each console needs its own copy, so the second is made from the first */
	char p0[600];
	snprintf(p0, sizeof(p0), "%s\\cores\\pslink_core.dll", base_dir);
	snprintf(path, sizeof(path), "%s\\cores\\pslink_core_%d.dll", base_dir, c + 1);
	CopyFileA(p0, path, FALSE);
	k->lib = LoadLibraryA(path);
	if (!k->lib) fail("Could not load %s (error %lu)", path, GetLastError());
#else
	/* Linux: the same library twice, each in its own namespace (dlmopen) so nothing is shared */
	snprintf(path, sizeof(path), "%s/cores/pslink_core.so", base_dir);
	k->lib = dlmopen(LM_ID_NEWLM, path, RTLD_NOW | RTLD_LOCAL);
	if (!k->lib) fail("Could not load %s: %s", path, dlerror());
#endif
	k->init = sym(k, "retro_init"); k->deinit = sym(k, "retro_deinit"); k->run = sym(k, "retro_run"); k->reset = sym(k, "retro_reset");
	k->set_env = sym(k, "retro_set_environment"); k->set_video = sym(k, "retro_set_video_refresh");
	k->set_audio = sym(k, "retro_set_audio_sample"); k->set_audio_batch = sym(k, "retro_set_audio_sample_batch");
	k->set_poll = sym(k, "retro_set_input_poll"); k->set_state = sym(k, "retro_set_input_state");
	k->load_game = sym(k, "retro_load_game"); k->unload_game = sym(k, "retro_unload_game");
	k->get_av = sym(k, "retro_get_system_av_info"); k->set_port = sym(k, "retro_set_controller_port_device");
	k->sio_set_tx = sym(k, "pslink_sio1_set_tx"); k->sio_rx = sym(k, "pslink_sio1_rx"); k->sio_connect = sym(k, "pslink_sio1_connect");
}

/* ---------------- controls ---------------- */
enum { B_CROSS, B_SQUARE, B_SELECT, B_START, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_CIRCLE, B_TRIANGLE, B_L1, B_R1, B_L2, B_R2, B_L3, B_R3,
       A_LUP, A_LDOWN, A_LLEFT, A_LRIGHT, A_RUP, A_RDOWN, A_RLEFT, A_RRIGHT, NCTL };
static const char *ctl_names[NCTL] = { "cross", "square", "select", "start", "up", "down", "left", "right", "circle", "triangle",
	"l1", "r1", "l2", "r2", "l3", "r3", "move_forward", "move_back", "strafe_left", "strafe_right", "look_up", "look_down", "turn_left", "turn_right" };
/* the keyboard: console 1 on the left of the keyboard, console 2 on the arrows and the number pad */
static const char *key_default[2][NCTL] = {
	{ "Space", "T", "Tab", "Return", "", "", "", "", "C", "R", "Q", "E", "G", "F", "", "", "W", "S", "A", "D", "I", "K", "J", "L" },
	{ "Keypad 0", "Keypad +", "Keypad -", "Keypad Enter", "", "", "", "", "Keypad 1", "Keypad .", "Keypad 7", "Keypad 9", "Keypad 3", "Right Ctrl", "", "",
	  "Up", "Down", "Left", "Right", "Keypad 8", "Keypad 5", "Keypad 4", "Keypad 6" } };
static SDL_Scancode keymap[2][NCTL][2];
static SDL_GameController *pads[8]; static SDL_JoystickID pad_id[8]; static int npads;

static void pads_open(void) {
	int i;
	for (i = 0; i < npads; i++) if (pads[i]) SDL_GameControllerClose(pads[i]);
	npads = 0;
	for (i = 0; i < SDL_NumJoysticks() && npads < 8; i++)
		if (SDL_IsGameController(i) && (pads[npads] = SDL_GameControllerOpen(i))) {
			pad_id[npads] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[npads]));
			say("controller %d: %s\n", npads + 1, SDL_GameControllerName(pads[npads]));
			npads++;
		}
}

static int16_t clamp16(int v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
static void read_pads(void) {
	const Uint8 *ks = SDL_GetKeyboardState(NULL); int c, i;
	for (c = 0; c < 2; c++) {
		Console *k = &con[c]; uint32_t b = 0; int ax[4] = { 0, 0, 0, 0 };
		int p = swap_pads ? 1 - c : c;
		for (i = 0; i < NCTL; i++) {
			int on = (keymap[c][i][0] && ks[keymap[c][i][0]]) || (keymap[c][i][1] && ks[keymap[c][i][1]]);
			if (!on) continue;
			if (i <= B_R3) b |= 1u << i;
			else switch (i) {
				case A_LUP: ax[1] -= 32767; break; case A_LDOWN: ax[1] += 32767; break;
				case A_LLEFT: ax[0] -= 32767; break; case A_LRIGHT: ax[0] += 32767; break;
				case A_RUP: ax[3] -= 32767; break; case A_RDOWN: ax[3] += 32767; break;
				case A_RLEFT: ax[2] -= 32767; break; case A_RRIGHT: ax[2] += 32767; break;
			}
		}
		if (p < npads && pads[p]) {
			SDL_GameController *g = pads[p];
			static const struct { int sdl, ps; } bm[] = {
				{ SDL_CONTROLLER_BUTTON_A, B_CROSS }, { SDL_CONTROLLER_BUTTON_B, B_CIRCLE }, { SDL_CONTROLLER_BUTTON_X, B_SQUARE },
				{ SDL_CONTROLLER_BUTTON_Y, B_TRIANGLE }, { SDL_CONTROLLER_BUTTON_BACK, B_SELECT }, { SDL_CONTROLLER_BUTTON_START, B_START },
				{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, B_L1 }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, B_R1 },
				{ SDL_CONTROLLER_BUTTON_LEFTSTICK, B_L3 }, { SDL_CONTROLLER_BUTTON_RIGHTSTICK, B_R3 },
				{ SDL_CONTROLLER_BUTTON_DPAD_UP, B_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, B_DOWN },
				{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, B_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, B_RIGHT } };
			for (i = 0; i < (int)(sizeof(bm) / sizeof(bm[0])); i++) if (SDL_GameControllerGetButton(g, bm[i].sdl)) b |= 1u << bm[i].ps;
			if (SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) b |= 1u << B_L2;
			if (SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) b |= 1u << B_R2;
			ax[0] += SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTX); ax[1] += SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTY);
			ax[2] += SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_RIGHTX); ax[3] += SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_RIGHTY);
		}
		if (k->script) b |= k->sbuttons;
		k->buttons = b;
		for (i = 0; i < 4; i++) k->axis[i] = clamp16(ax[i]);
	}
}

/* scripted pads for testing: "<frame> <hex libretro joypad mask>" from that frame on, or "<frame> shot <file>" */
static void script_step(int c) {
	Console *k = &con[c];
	while (k->script) {
		if (!k->sline[0]) { if (!fgets(k->sline, sizeof(k->sline), k->script)) { fclose(k->script); k->script = NULL; break; } k->snext = atoi(k->sline); }
		if (k->snext > frame_no) break;
		{ char *s = strchr(k->sline, ' ');
		  if (s && !strncmp(s + 1, "shot", 4)) { char fn[400]; if (sscanf(s + 6, "%399s", fn) == 1) { extern void shot_console(int, const char *); shot_console(c, fn); } }
		  else if (s) k->sbuttons = (uint32_t)strtoul(s + 1, NULL, 16); }
		k->sline[0] = 0;
	}
}

/* ---------------- settings: pslink.ini beside the program ---------------- */
static char disc[2][512];
static void ini_load(void) {
	char path[600], line[600]; FILE *f; int c, i;
	for (c = 0; c < 2; c++) for (i = 0; i < NCTL; i++) keymap[c][i][0] = key_default[c][i][0] ? SDL_GetScancodeFromName(key_default[c][i]) : 0;
	snprintf(path, sizeof(path), "%s/pslink.ini", base_dir);
	f = fopen(path, "r");
	if (!f) {                                  /* first run: write one with every setting, to be edited */
		f = fopen(path, "w");
		if (!f) return;
		fprintf(f, "; PSLink settings.  Keys are SDL key names (A, Space, Left Shift, Keypad 5, F, ...); two per control, comma separated.\n");
		fprintf(f, "; bios = hle (built in, no file needed) or auto (a real BIOS such as scph1001.bin in the system folder)\n");
		fprintf(f, "bios = hle\n; layout = side (side by side) or stack (one above the other)\nlayout = side\n; audio = both, 1 or 2\naudio = both\n");
		fprintf(f, "; the last disc played (used when PSLink is started without one)\ndisc =\n\n");
		for (c = 0; c < 2; c++) {
			fprintf(f, "[console %d keyboard]\n", c + 1);
			for (i = 0; i < NCTL; i++) fprintf(f, "%s = %s\n", ctl_names[i], key_default[c][i]);
			fprintf(f, "\n");
		}
		fclose(f);
		return;
	}
	c = -1;
	while (fgets(line, sizeof(line), f)) {
		char *e, *k = line, *v; int n;
		while (isspace((unsigned char)*k)) k++;
		if (*k == ';' || !*k) continue;
		if (*k == '[') { c = strstr(k, "2") ? 1 : strstr(k, "1") ? 0 : -1; continue; }
		if (!(e = strchr(k, '='))) continue;
		*e = 0; v = e + 1;
		for (n = (int)strlen(k); n && isspace((unsigned char)k[n - 1]); ) k[--n] = 0;
		while (isspace((unsigned char)*v)) v++;
		for (n = (int)strlen(v); n && isspace((unsigned char)v[n - 1]); ) v[--n] = 0;
		if (c < 0) {
			if (!strcmp(k, "bios")) use_hle = strcmp(v, "auto") != 0;
			else if (!strcmp(k, "layout")) layout = !strcmp(v, "stack");
			else if (!strcmp(k, "audio")) audio_mode = !strcmp(v, "1") ? 1 : !strcmp(v, "2") ? 2 : 0;
			else if (!strcmp(k, "disc") && *v && !disc[0][0]) snprintf(disc[0], sizeof(disc[0]), "%s", v);
			continue;
		}
		for (i = 0; i < NCTL; i++) if (!strcmp(k, ctl_names[i])) {
			char *a = v, *b = strchr(v, ',');
			if (b) { *b++ = 0; while (isspace((unsigned char)*b)) b++; }
			for (n = (int)strlen(a); n && isspace((unsigned char)a[n - 1]); ) a[--n] = 0;
			keymap[c][i][0] = *a ? SDL_GetScancodeFromName(a) : 0;
			keymap[c][i][1] = b && *b ? SDL_GetScancodeFromName(b) : 0;
			if (*a && !keymap[c][i][0]) say("pslink.ini: unknown key \"%s\"\n", a);
		}
	}
	fclose(f);
}
static void ini_save_disc(const char *d) {      /* remember the disc: rewrite the disc = line */
	char path[600], *buf; FILE *f; long n; char *p, *e;
	snprintf(path, sizeof(path), "%s/pslink.ini", base_dir);
	if (!(f = fopen(path, "rb"))) return;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	buf = calloc(1, n + 1); if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); return; } fclose(f);
	p = strstr(buf, "\ndisc =");
	if (!p || !(f = fopen(path, "wb"))) { free(buf); return; }
	e = strchr(p + 1, '\n'); if (!e) e = buf + n;
	fwrite(buf, 1, p + 1 - buf, f); fprintf(f, "disc = %s", d); fwrite(e, 1, buf + n - e, f);
	fclose(f); free(buf);
}

/* ---------------- drawing ---------------- */
static void screen_rect(int c, int ww, int wh, SDL_Rect *r) {
	int sw = layout ? ww : ww / 2, sh = layout ? wh / 2 : wh, w = sw, h = sw * 3 / 4;
	if (h > sh) { h = sh; w = sh * 4 / 3; }
	r->w = w; r->h = h;
	r->x = (layout ? 0 : c * sw) + (sw - w) / 2; r->y = (layout ? c * sh : 0) + (sh - h) / 2;
}
static void draw(void) {
	int ww, wh, c;
	if (!ren) return;
	SDL_GetRendererOutputSize(ren, &ww, &wh);
	SDL_SetRenderDrawColor(ren, 12, 12, 16, 255); SDL_RenderClear(ren);
	for (c = 0; c < 2; c++) {
		SDL_Rect s = { 0, 0, (int)con[c].w, (int)con[c].h }, d;
		screen_rect(c, ww, wh, &d);
		if (con[c].tex && con[c].w) SDL_RenderCopy(ren, con[c].tex, &s, &d);
		if (paused) { SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(ren, 0, 0, 0, 120); SDL_RenderFillRect(ren, &d); }
	}
	SDL_SetRenderDrawColor(ren, 60, 60, 70, 255);
	if (layout) SDL_RenderDrawLine(ren, 0, wh / 2, ww, wh / 2); else SDL_RenderDrawLine(ren, ww / 2, 0, ww / 2, wh);
	SDL_RenderPresent(ren);
}
void shot_console(int c, const char *fn) {     /* the console's own picture, as a PPM */
	int ww, wh; SDL_Rect d; uint8_t *px; FILE *f; int y;
	if (!ren) return;
	draw();
	SDL_GetRendererOutputSize(ren, &ww, &wh); screen_rect(c, ww, wh, &d);
	px = malloc(d.w * d.h * 3);
	if (SDL_RenderReadPixels(ren, &d, SDL_PIXELFORMAT_RGB24, px, d.w * 3) == 0 && (f = fopen(fn, "wb"))) {
		fprintf(f, "P6 %d %d 255\n", d.w, d.h);
		for (y = 0; y < d.h; y++) fwrite(px + y * d.w * 3, 1, d.w * 3, f);
		fclose(f);
	}
	free(px);
}
static void title(double fps) {
	char t[300];
	static const char *am[3] = { "both", "console 1", "console 2" };
	snprintf(t, sizeof(t), "PSLink %s - %s%s - link cable %s (%ld / %ld bytes) - sound: %s - %.0f fps   [F1 help]", VERSION,
	         npads >= 2 ? "two controllers" : npads == 1 ? "one controller + keyboard" : "keyboard",
	         swap_pads ? " (swapped)" : "", linked ? "in" : "OUT", con[0].sent, con[1].sent, am[audio_mode], fps);
	if (paused) strcat(t, " - PAUSED");
	SDL_SetWindowTitle(win, t);
}
static void help(void) {
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "PSLink help",
		"Two PlayStations on a link cable.\n\n"
		"Controllers: the first plays console 1 (left), the second console 2 (right).\n\n"
		"Keyboard, console 1: W A S D move, J L turn, I K look, Space = X, F = R2, G = L2, Q / E = L1 / R1,\n"
		"   R = Triangle, T = Square, C = Circle, Enter = Start, Tab = Select\n"
		"Keyboard, console 2: arrow keys move, number pad 4 6 turn, 8 5 look, 0 = X, Right Ctrl = R2,\n"
		"   pad 3 = L2, pad 7 / 9 = L1 / R1, pad . = Triangle, pad + = Square, pad 1 = Circle, pad Enter = Start, pad - = Select\n"
		"(change them in pslink.ini)\n\n"
		"F1 this help   F2 reset both   F4 swap controllers   F5 sound: both / 1 / 2\n"
		"F6 side by side / stacked   F7 unplug / plug the link cable   F9 pause   F11 full screen\n"
		"Drop a .cue file on the window to load it on both consoles.", win);
}

/* ---------------- the disc ---------------- */
static int game_loaded[2];
static void load_disc(int c, const char *path) {
	Console *k = &con[c]; struct retro_game_info gi;
	if (game_loaded[c]) { k->unload_game(); game_loaded[c] = 0; }
	memset(&gi, 0, sizeof(gi)); gi.path = path;
	if (!k->load_game(&gi)) fail("Console %d could not load %s", c + 1, path);
	k->set_port(0, RETRO_DEVICE_PSE_ANALOG);    /* an analog pad: the sticks work, and so does the D-pad */
	k->sio_connect(linked);
	game_loaded[c] = 1;
	say("console %d: %s\n", c + 1, path);
}
static int pick_disc(char *out, int n) {
#ifdef _WIN32
	OPENFILENAMEA o; memset(&o, 0, sizeof(o)); out[0] = 0;
	o.lStructSize = sizeof(o); o.lpstrFilter = "PlayStation discs (*.cue;*.bin;*.chd;*.iso)\0*.cue;*.bin;*.chd;*.iso\0All files\0*.*\0";
	o.lpstrFile = out; o.nMaxFile = n; o.lpstrTitle = "PSLink: choose a disc (both consoles play it)";
	o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	return GetOpenFileNameA(&o) != 0;
#else
	(void)out; (void)n; return 0;
#endif
}

int main(int argc, char **argv) {
	struct retro_system_av_info av; int c, i, ndisc = 0; double fps; Uint64 t_next, per; int fps_frames = 0; Uint32 fps_t;
	char *bp = SDL_GetBasePath();
	snprintf(base_dir, sizeof(base_dir), "%s", bp ? bp : "./");
	{ size_t n = strlen(base_dir); if (n && (base_dir[n - 1] == '/' || base_dir[n - 1] == '\\')) base_dir[n - 1] = 0; }
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames_max = atoi(argv[++i]); headless = getenv("PSLINK_SHOW") ? 0 : 1; }
		else if ((!strcmp(argv[i], "--script1") || !strcmp(argv[i], "--script2")) && i + 1 < argc) { int k = argv[i][8] - '1'; con[k].script = fopen(argv[++i], "r"); }
		else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_prefix = argv[++i];
		else if (ndisc < 2) snprintf(disc[ndisc++], sizeof(disc[0]), "%s", argv[i]);
	}
	snprintf(log_path, sizeof(log_path), "%s/pslink.log", base_dir); log_file = fopen(log_path, "w");
	snprintf(sys_dir, sizeof(sys_dir), "%s/system", base_dir); mkdir_(sys_dir);
	{ char sd[600]; snprintf(sd, sizeof(sd), "%s/saves", base_dir); mkdir_(sd); }
	for (c = 0; c < 2; c++) { snprintf(con[c].savedir, sizeof(con[c].savedir), "%s/saves/console%d", base_dir, c + 1); mkdir_(con[c].savedir); }
	if (headless) { SDL_setenv("SDL_VIDEODRIVER", "offscreen", 0); SDL_setenv("SDL_AUDIODRIVER", "dummy", 0); }
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) fail("SDL: %s", SDL_GetError());
	ini_load();
	if (!disc[0][0] && !pick_disc(disc[0], sizeof(disc[0]))) fail("PSLink needs a PlayStation disc image: start it with a .cue file (or drop one on pslink.exe).");
	if (!disc[1][0]) snprintf(disc[1], sizeof(disc[1]), "%s", disc[0]);

	win = SDL_CreateWindow("PSLink", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, layout ? 640 : 1280, layout ? 960 : 480, SDL_WINDOW_RESIZABLE | (headless ? SDL_WINDOW_HIDDEN : 0));
	if (!win) fail("SDL window: %s", SDL_GetError());
	ren = SDL_CreateRenderer(win, -1, headless ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	if (!ren) fail("SDL renderer: %s", SDL_GetError());
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	pads_open();

	for (c = 0; c < 2; c++) {
		Console *k = &con[c];
		core_open(c);
		k->set_env(c ? env1 : env0);
		k->init();
		k->set_video(c ? video1 : video0); k->set_audio(c ? sample1 : sample0); k->set_audio_batch(c ? batch1 : batch0);
		k->set_poll(c ? poll1 : poll0); k->set_state(c ? state1 : state0);
		k->sio_set_tx(c ? tx1 : tx0);
		load_disc(c, disc[c]);
	}
	if (!headless && ndisc) ini_save_disc(disc[0]);
	con[0].get_av(&av);
	fps = av.timing.fps > 1 ? av.timing.fps : 59.94;
	{
		SDL_AudioSpec want, have; memset(&want, 0, sizeof(want));
		want.freq = av.timing.sample_rate > 1 ? (int)av.timing.sample_rate : 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
		adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
		if (adev) SDL_PauseAudioDevice(adev, 0); else say("no sound: %s\n", SDL_GetError());
	}
	say("PSLink %s: %.2f fps, %d Hz, %d controller(s)\n", VERSION, fps, (int)av.timing.sample_rate, npads);

	per = (Uint64)(SDL_GetPerformanceFrequency() / fps); t_next = SDL_GetPerformanceCounter(); fps_t = SDL_GetTicks();
	for (;;) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) goto done;
			if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) pads_open();
			if (e.type == SDL_DROPFILE) { for (c = 0; c < 2; c++) load_disc(c, e.drop.file); ini_save_disc(e.drop.file); SDL_free(e.drop.file); }
			if (e.type == SDL_KEYDOWN && !e.key.repeat) switch (e.key.keysym.sym) {
				case SDLK_F1: help(); break;
				case SDLK_F2: for (c = 0; c < 2; c++) { con[c].reset(); con[c].nout = 0; } break;
				case SDLK_F4: swap_pads ^= 1; break;
				case SDLK_F5: audio_mode = (audio_mode + 1) % 3; break;
				case SDLK_F6: layout ^= 1; break;
				case SDLK_F7: linked ^= 1; for (c = 0; c < 2; c++) { con[c].sio_connect(linked); con[c].nout = 0; } break;
				case SDLK_F9: paused ^= 1; break;
				case SDLK_F11: SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); break;
			}
		}
		if (!paused) {
			for (c = 0; c < 2; c++) script_step(c);
			read_pads();
			/* each console gets what the other sent during its last frame, then runs one */
			for (c = 0; c < 2; c++) {
				Console *o = &con[1 - c];
				if (linked && o->nout) { con[c].sio_rx(o->out, o->nout); con[c].got += o->nout; }
				o->nout = 0;
				con[c].run();
			}
			{   /* the sound: both consoles mixed (or one), into SDL's queue - not more than a fifth of a second ahead */
				int n = con[0].naudio < con[1].naudio ? con[0].naudio : con[1].naudio; static int16_t mix[16384];
				if (audio_mode) n = con[audio_mode - 1].naudio;
				for (i = 0; i < n * 2; i++) {
					int v = audio_mode ? con[audio_mode - 1].audio[i] : (con[0].audio[i] + con[1].audio[i]) * 5 / 8;
					mix[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
				}
				if (adev && SDL_GetQueuedAudioSize(adev) < 44100 * 4 / 5) SDL_QueueAudio(adev, mix, n * 4);
				for (c = 0; c < 2; c++) {
					int used = audio_mode && audio_mode - 1 != c ? con[c].naudio : n;
					if (used > con[c].naudio) used = con[c].naudio;
					memmove(con[c].audio, con[c].audio + used * 2, (con[c].naudio - used) * 4); con[c].naudio -= used;
					if (con[c].naudio > 4096) con[c].naudio = 0;   /* (one console far behind the other: start over) */
				}
			}
			frame_no++;
			if (frames_max >= 0 && frame_no >= frames_max) break;
		}
		if (!headless) {
			Uint64 now;
			draw();
			fps_frames++;
			if (SDL_GetTicks() - fps_t >= 1000) { title(fps_frames * 1000.0 / (SDL_GetTicks() - fps_t)); fps_frames = 0; fps_t = SDL_GetTicks(); }
			t_next += per; now = SDL_GetPerformanceCounter();
			if (t_next > now) { Uint64 ms = (t_next - now) * 1000 / SDL_GetPerformanceFrequency(); if (ms > 1) SDL_Delay((Uint32)(ms - 1)); while (SDL_GetPerformanceCounter() < t_next) ; }
			else if (now - t_next > per * 8) t_next = now;      /* far behind (a slow PC): don't try to catch up */
		}
	}
done:
	if (shot_prefix) { char fn[600]; for (c = 0; c < 2; c++) { snprintf(fn, sizeof(fn), "%s%d.ppm", shot_prefix, c + 1); shot_console(c, fn); } }
	say("link cable: console 1 sent %ld bytes, console 2 sent %ld\n", con[0].sent, con[1].sent);
	for (c = 0; c < 2; c++) { if (game_loaded[c]) con[c].unload_game(); con[c].deinit(); }
	SDL_Quit();
	return 0;
}
