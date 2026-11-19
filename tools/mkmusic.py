#!/usr/bin/env python3
"""Build MUSIC.XA: one track per map (Doom style) plus the title, as CD-XA ADPCM (37.8 kHz stereo, 4 bit), 8 channels
interleaved for 2x speed reading.  Tracks are laid end to end inside a channel (with a second of silence between them),
the channels balanced so the file is as short as possible.  Also writes src/music_tab.h for the game.

usage: mkmusic.py pcm_dir out.xa   (pcm_dir: <name>.raw, 16-bit stereo 37800 Hz, made by tools/musicprep.sh)
"""
import sys, os, subprocess, struct, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RATE, ROWS_PER_S, CH = 37800, 18.75, 8          # 2x speed: 150 sectors/s, each channel gets 1 in 8 -> 18.75/s
FRAMES_PER_SECTOR = 2016
GAP_ROWS = 19                                     # ~1 s of silence after each track in its channel
MAXLEN = 300                                      # seconds; longer tracks are cut here with a fade

# track 0 = menus / title; then one per map, by catalog name (save.c's catalog order)
TITLE = 'revenge'
MAP_TRACKS = [
    ('pig01', '02_fanatic-conquerer'),   ('pig02', '01_fanatic-assault'),        ('pig03', '05_fanatic-lost-in-the-mist'),
    ('pig04', '03_fanatic-tranquility'), ('pig05', '04_fanatic-unreleased-rage'), ('pig06', '08_fanatic-waiting-to-strike'),
    ('pig07', '09_fanatic-disrupting-force'), ('pig08', '06_fanatic-hit-them-hard'), ('pig09', 'ambient'),
    ('pigfin', '10_fanatic-my-destiny'), ('cruel01', '07_fanatic-cubed-to-death'), ('cruel02', 'deathB'),
    ('wsg1', 'cube1'), ('wsg2', 'cubez0r'), ('wsg3', '11_fanatic-set-free'),
]
CATALOG = ['pig01', 'pig02', 'pig03', 'pig04', 'pig05', 'pig06', 'pig07', 'pig08', 'pig09', 'pigfin', 'cruel01', 'cruel02', 'wsg1', 'wsg2', 'wsg3']

def main():
    src, out = sys.argv[1], sys.argv[2]
    names = [TITLE] + [t for _, t in MAP_TRACKS]
    assert len(set(names)) == len(names), 'every map gets its own track'
    import tempfile; tmp = tempfile.mkdtemp(prefix='xa_')
    xaenc = os.path.join(tmp, 'xaenc')
    subprocess.check_call(['gcc', '-O2', '-o', xaenc, os.path.join(HERE, 'xaenc.c')])
    tracks = []
    for n in names:
        a = np.fromfile(os.path.join(src, n + '.raw'), '<i2').reshape(-1, 2)
        if len(a) > MAXLEN * RATE:                # cut with a 6 s fade
            a = a[:MAXLEN * RATE].astype(np.float32); f = 6 * RATE
            a[-f:] *= np.linspace(1, 0, f)[:, None]; a = a.astype('<i2')
        p = os.path.join(tmp, n + '.raw'); a.tofile(p)
        x = os.path.join(tmp, n + '.xas')
        subprocess.check_call([xaenc, p, x, '1', '0'], stderr=subprocess.DEVNULL)
        secs = open(x, 'rb').read(); rows = len(secs) // 2336
        tracks.append(dict(name=n, data=secs, rows=rows))
    # longest first onto the emptiest channel
    fill = [0] * CH; place = {}
    for i in sorted(range(len(tracks)), key=lambda i: -tracks[i]['rows']):
        c = min(range(CH), key=lambda c: fill[c])
        place[i] = (c, fill[c]); fill[c] += tracks[i]['rows'] + GAP_ROWS
    total = max(fill)
    chans = [[None] * total for _ in range(CH)]
    for i, t in enumerate(tracks):
        c, r = place[i]
        for k in range(t['rows']): chans[c][r + k] = t['data'][k * 2336:(k + 1) * 2336]
    def silence(c):
        s = bytearray(2336); s[0] = s[4] = 1; s[1] = s[5] = c; s[2] = s[6] = 0x64; s[3] = s[7] = 0x01; return bytes(s)
    with open(out, 'wb') as f:
        for r in range(total):
            for c in range(CH):
                sec = chans[c][r]
                if sec is None: sec = silence(c)
                else: sec = sec[:1] + bytes([c]) + sec[2:5] + bytes([c]) + sec[6:]      # this channel's number, and
                sec = bytearray(sec); sec[2] &= 0x7e; sec[6] &= 0x7e; f.write(bytes(sec))   # no EOF / EOR marks
    # the game's table
    with open(os.path.join(HERE, '..', 'src', 'music_tab.h'), 'w') as h:
        h.write('/* made by tools/mkmusic.py - MUSIC.XA layout: channel, first row (8 sectors a row), rows */\n')
        h.write('#define MUSIC_TRACKS %d\n#define MUSIC_TITLE 0\n' % len(tracks))
        h.write('static const struct { uint8_t chan; uint16_t row, rows; } music_tracks[MUSIC_TRACKS] = {\n')
        for i, t in enumerate(tracks):
            c, r = place[i]; h.write('\t{ %d, %d, %d },   /* %s, %.0f s */\n' % (c, r, t['rows'], t['name'], t['rows'] / ROWS_PER_S))
        h.write('};\n/* catalog index -> track */\nstatic const uint8_t music_of_cat[%d] = { %s };\n' % (
            len(CATALOG), ', '.join(str(1 + [m for m, _ in MAP_TRACKS].index(c)) for c in CATALOG)))
    print('%s: %d tracks, %d rows x 8 sectors = %.1f MB, channels filled %s' % (out, len(tracks), total, total * 8 * 2336 / 1e6,
          ' '.join('%.0f%%' % (100 * f / total) for f in fill)))

main()
