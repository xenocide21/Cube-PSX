/* link.c - the link cable (serial port, SIO1, at 115200 baud) and lockstep play.
 *
 * Packets: 0xA5, type, length, payload, checksum (the sum of type, length and payload).  A broken packet is dropped
 * and the reader looks for the next 0xA5.
 *
 *   HELLO   every half second while the link screen is up: "a console is here"
 *   SETUP   the host's game (LinkSetup); the other console, if it is on the link screen, answers ACCEPT with its own
 *           look settings, and both load the map
 *   READY   loaded (repeated until the other side is seen ready or playing)
 *   INPUT   one tick's pad (tick number, buttons held, pressed, both sticks)
 *   SYNC    every 64 ticks, the game's hash at that tick: if the two consoles ever disagree, the match stops
 *   QUIT    left the game
 *
 * Lockstep: tick t runs when both pads for t are known.  Each console's pad goes out LINK_DELAY ticks ahead of when it
 * is used, so the cable has a few frames to carry it; the first LINK_DELAY ticks run on empty pads. */
#include "cube.h"
#include <string.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <psxsio.h>

enum { P_HELLO = 1, P_SETUP, P_ACCEPT, P_READY, P_INPUT = 0x10, P_SYNC, P_QUIT };
#define LINK_DELAY 4
#define QN 64

static int opened, state, me, frames, hello_at, ask_at, ready_at, listen_at = -1000;
static LinkSetup setup_got;
static int got_setup, got_accept, peer_ready, peer_quit;
static uint32_t peer_seen;                  /* the frame we last heard anything */
static const char *err;

/* incoming bytes: the serial interrupt drops them into a ring big enough for a long CD load */
#define RXN 1024
static volatile uint8_t rx[RXN]; static volatile uint16_t rx_w; static uint16_t rx_r;
static int on_byte(uint8_t b) { rx[rx_w] = b; rx_w = (rx_w + 1) & (RXN - 1); return 1; }

/* outgoing: our own queue in front of the library's small one */
#define TXN 512
static uint8_t tx[TXN]; static int tx_r, tx_w;
static void tx_pump(void) { while (tx_r != tx_w && SIO_WriteByte2(tx[tx_r]) >= 0) tx_r = (tx_r + 1) & (TXN - 1); }
static void send(int type, const void *data, int n) {
	const uint8_t *d = data; int i, sum = type + n, room = (tx_r - tx_w - 1) & (TXN - 1);
	if (!opened || n + 4 > room) return;
	tx[tx_w] = 0xA5; tx_w = (tx_w + 1) & (TXN - 1);
	tx[tx_w] = type; tx_w = (tx_w + 1) & (TXN - 1);
	tx[tx_w] = n; tx_w = (tx_w + 1) & (TXN - 1);
	for (i = 0; i < n; i++) { tx[tx_w] = d[i]; sum += d[i]; tx_w = (tx_w + 1) & (TXN - 1); }
	tx[tx_w] = sum & 255; tx_w = (tx_w + 1) & (TXN - 1);
	tx_pump();
}

/* ---- the lockstep game ---- */
static LinkPad lin[QN], rin[QN];            /* pads by tick: ours, theirs */
static int gen_t, sim_t, rmax, acc_pressed, quit_req;
static uint32_t hash_at[8], peer_hash[8]; static int hash_tick[8], peer_tick[8];   /* ticks as sent: 16 bits */
static int stall;

static void on_packet(int type, const uint8_t *p, int n) {
	peer_seen = frames;
	switch (type) {
	case P_HELLO: break;
	case P_SETUP:
		if (n == sizeof(LinkSetup) && state == LS_IDLE) { memcpy(&setup_got, p, n); got_setup = 1; }
		break;
	case P_ACCEPT:
		if (n == 3 && state == LS_ASK) { setup_got.look[1] = p[0]; setup_got.invert[1] = p[1]; setup_got.aim[1] = p[2]; got_accept = 1; }
		break;
	case P_READY: if (state == LS_SYNC || state == LS_PLAY) peer_ready = 1; break;
	case P_INPUT:
		if (n == 11 && (state == LS_SYNC || state == LS_PLAY)) {
			int t = p[0] | p[1] << 8; LinkPad *d;
			peer_ready = 1;
			/* tick numbers wrap at 65536: take the one nearest what we expect */
			t = rmax + (int16_t)(t - rmax);
			if (t < rmax || t >= sim_t + QN) break;  /* old, or too far ahead (cannot happen if both play fair) */
			d = &rin[t & (QN - 1)];
			d->held = p[2] | p[3] << 8; d->pressed = p[4] | p[5] << 8; d->lx = p[6]; d->ly = p[7]; d->rx = p[8]; d->ry = p[9]; d->quit = p[10];
			if (t == rmax) rmax++;
		}
		break;
	case P_SYNC:
		if (n == 6) { int t = p[0] | p[1] << 8, k = (t >> 6) & 7; peer_tick[k] = t; peer_hash[k] = p[2] | p[3] << 8 | p[4] << 16 | (uint32_t)p[5] << 24; }
		break;
	case P_QUIT: peer_quit = n == 1 ? p[0] : 1; break;
	}
}

static void rx_parse(void) {
	static uint8_t buf[64]; static int st, type, len, got, sum;
	while (rx_r != rx_w) {
		int b = rx[rx_r]; rx_r = (rx_r + 1) & (RXN - 1);
		switch (st) {
		case 0: if (b == 0xA5) st = 1; break;
		case 1: type = b; sum = b; st = 2; break;
		case 2: len = b; sum += b; got = 0; st = len > (int)sizeof(buf) ? 0 : len ? 3 : 4; break;
		case 3: buf[got++] = b; sum += b; if (got == len) st = 4; break;
		case 4: if ((sum & 255) == b) on_packet(type, buf, len); st = 0; break;
		}
	}
}

void link_open(void) {
	if (opened) return;
	SIO_Init(115200, MR_CHLEN_8 | MR_SB_01);
	SIO_ReadCallback(on_byte);
	opened = 1; state = LS_IDLE; peer_seen = 0; frames = 1000;
}

void link_listen(void) { listen_at = frames; }
int link_peer(void) { return opened && frames - (int)peer_seen < 90; }
int link_state(void) { return state; }
const char *link_error(void) { return err; }

/* once a frame, whatever screen is up */
void link_poll(void) {
	if (!opened) return;
	frames++;
	rx_parse();
	tx_pump();
	if ((state == LS_IDLE && frames - listen_at < 60) || state == LS_ASK) {      /* (only while the link screen is up) */
		if (frames >= hello_at) { send(P_HELLO, 0, 0); hello_at = frames + 30; }
	}
	if (state == LS_ASK) {
		if (got_accept) { state = LS_LOAD; me = 0; }
		else if (frames >= ask_at + 180) { state = LS_IDLE; err = "THE OTHER CONSOLE IS NOT ON THE LINK SCREEN"; }
		else if (((frames - ask_at) % 20) == 0) send(P_SETUP, &setup_got, sizeof(LinkSetup));
	}
	if (state == LS_SYNC && frames >= ready_at) { send(P_READY, 0, 0); ready_at = frames + 10; }
	if (state == LS_SYNC && peer_ready) state = LS_PLAY;
	if ((state == LS_SYNC || state == LS_PLAY) && frames - (int)peer_seen > 300) { err = "THE LINK IS LOST"; link_stop(); }
	if (state == LS_PLAY && peer_quit) {
		err = match_over ? 0 : peer_quit == 2 ? "OUT OF STEP: THE TWO GAMES DIFFER" : peer_quit == 3 ? "THE LINK IS LOST" : "THE OTHER PLAYER LEFT";
		link_stop();
	}
}

/* the link screen: start a game as host; or, if the other console's offer is in, join it */
void link_host(const LinkSetup *s) {
	if (state != LS_IDLE) return;
	setup_got = *s; got_accept = 0; err = 0;
	state = LS_ASK; ask_at = frames;
	send(P_SETUP, &setup_got, sizeof(LinkSetup));
}
int link_offer(LinkSetup *s) {               /* the guest side: an offer came in; accept it */
	uint8_t a[3];
	if (state != LS_IDLE || !got_setup) return 0;
	got_setup = 0; err = 0;
	setup_got.look[1] = a[0] = opt_look; setup_got.invert[1] = a[1] = opt_invert; setup_got.aim[1] = a[2] = opt_aim;
	send(P_ACCEPT, a, 3);
	*s = setup_got; me = 1; state = LS_LOAD;
	return 1;
}
int link_agreed(LinkSetup *s, int *who) {     /* a game both have agreed to, once (then load it and call link_loaded) */
	if (state != LS_LOAD) return 0;
	*s = setup_got; *who = me; return 1;
}
void link_loaded(void) {
	int i;
	memset(lin, 0, sizeof(lin)); memset(rin, 0, sizeof(rin));
	gen_t = rmax = LINK_DELAY; sim_t = 0; acc_pressed = 0; quit_req = 0;
	for (i = 0; i < 8; i++) hash_tick[i] = peer_tick[i] = -1;
	peer_ready = peer_quit = 0; err = 0;
	state = LS_SYNC; ready_at = frames;
	rx_parse();                                /* (what came in while loading: maybe their READY already) */
}
void link_stop(void) {
	if (state == LS_PLAY || state == LS_SYNC) {       /* tell the other console why: 1 left, 2 out of step, 3 lost */
		uint8_t why = !err || peer_quit ? 1 : err[0] == 'O' ? 2 : 3;
		send(P_QUIT, &why, 1); tx_pump();
		if (err) game_link_abort(err);
	}
	state = LS_IDLE; got_setup = got_accept = 0;
}

/* a frame of play: our pad for the ticks this frame took, then every tick both pads are in for */
int link_frame(int held, int pressed, int lx, int ly, int rx_, int ry, int ticks) {
	int k, ran = 0;
	if (state != LS_PLAY) return 0;
	acc_pressed |= pressed;
	for (k = 0; k < ticks && gen_t - sim_t < LINK_DELAY + 4; k++) {      /* (not too far ahead of the other console) */
		LinkPad *p = &lin[gen_t & (QN - 1)]; uint8_t b[11];
		p->held = held; p->pressed = acc_pressed; acc_pressed = 0;
		p->lx = lx; p->ly = ly; p->rx = rx_; p->ry = ry; p->quit = quit_req;
		b[0] = gen_t; b[1] = gen_t >> 8; b[2] = p->held; b[3] = p->held >> 8; b[4] = p->pressed; b[5] = p->pressed >> 8;
		b[6] = p->lx; b[7] = p->ly; b[8] = p->rx; b[9] = p->ry; b[10] = p->quit;
		send(P_INPUT, b, 11);
		gen_t++;
	}
	while (sim_t < gen_t && sim_t < rmax && ran < 8 && state == LS_PLAY) {
		const LinkPad *mine = &lin[sim_t & (QN - 1)], *theirs = &rin[sim_t & (QN - 1)];
		game_tick_link(me == 0 ? mine : theirs, me == 0 ? theirs : mine);
		sim_t++; ran++;
		if ((sim_t & 63) == 0) {                 /* both consoles must still be playing the same game */
			uint32_t h = game_link_hash(); uint8_t b[6]; int s = (sim_t >> 6) & 7;
			hash_tick[s] = sim_t & 0xffff; hash_at[s] = h;
			b[0] = sim_t; b[1] = sim_t >> 8; b[2] = h; b[3] = h >> 8; b[4] = h >> 16; b[5] = h >> 24;
			send(P_SYNC, b, 6);
		}
	}
	stall = ran ? 0 : stall + 1;
	if (stall > 300) { err = "THE LINK IS LOST"; link_stop(); return ran; }    /* five seconds without the other's pad */
	for (k = 0; k < 8; k++)
		if (peer_tick[k] >= 0 && peer_tick[k] == hash_tick[k] && peer_hash[k] != hash_at[k]) {
			err = "OUT OF STEP: THE TWO GAMES DIFFER"; link_stop(); break;
		}
	return ran;
}
int link_waiting(void) { return state == LS_PLAY && sim_t >= rmax; }   /* stalled for the other console */
void link_quit(void) { quit_req = 1; }
