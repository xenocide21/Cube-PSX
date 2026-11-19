#!/usr/bin/env python3
"""Front-end walkthrough from power-on, through the pad: splash, title + demo, options (controls, sound), a new profile
(slot, name keyboard, difficulty), the map selector, a map, pause and save, finishing a map (next one opens), load.
usage: frontend.py elf out.png"""
import sys, struct
sys.path.insert(0, '.')
from rig import Rig
from PIL import Image, ImageDraw
P = dict(UP=16, RIGHT=32, DOWN=64, LEFT=128, START=8, CROSS=16384, CIRCLE=8192, TRI=4096, SQUARE=32768, SELECT=1, R2=512)
r = Rig(sys.argv[1], '../maps'); r.start()
shots = []
def run(n): r.run_frames(n)
def press(b, after=4):
    r.pad['btn'] = P[b]; run(1); r.pad['btn'] = 0; run(after)
def shot(tag):
    shots.append((tag, r.screenshot(None, 1).crop((0, 0, 320, 240))))
    print('%-34s frame %4d  card %s' % (tag, r.frames, {k: len(v) for k, v in (r.card or {}).items()}))
def i32(name): return struct.unpack('<i', r.rd(r.sym[name], 4))[0]
def state(): return i32('menu_state')

run(20); press('CROSS', 2)                          # skip the splash
while state() == 0: run(1)
run(90); shot('title menu')
press('DOWN'); press('DOWN'); press('CROSS'); shot('options')
press('CROSS'); press('RIGHT'); press('RIGHT'); press('DOWN'); press('DOWN'); press('RIGHT'); press('RIGHT'); press('DOWN'); press('CROSS'); shot('options > controls')
press('CIRCLE'); press('DOWN'); press('CROSS'); press('DOWN'); press('LEFT'); shot('options > sound')
press('CIRCLE'); press('DOWN'); press('CROSS'); press('CROSS'); shot('options > gameplay (aim assist off)'); press('CROSS')
press('CIRCLE'); press('CIRCLE'); press('UP'); press('UP')
press('CROSS'); shot('new game: pick a slot')
press('CROSS')
for ch in 'DUDE':                                  # type on the keyboard
    rows = ['ABCDEFGHIJ', 'KLMNOPQRST', 'UVWXYZ-.!?', '0123456789']
    ky, kx = next((y, row.index(ch)) for y, row in enumerate(rows) if ch in row)
    cy = struct.unpack('<i', r.rd(r.sym['kb_y'], 4))[0] if 'kb_y' in r.sym else 0
    cx = struct.unpack('<i', r.rd(r.sym['kb_x'], 4))[0] if 'kb_x' in r.sym else 0
    for _ in range((ky - cy) % 5): press('DOWN', 2)
    for _ in range((kx - cx) % 10): press('RIGHT', 2)
    press('CROSS', 2)
shot('name: DUDE typed')
press('START'); shot('difficulty (Normal)')
press('DOWN', 60); shot('difficulty: Hard (announcer)')
press('CROSS', 30); shot('hub')
press('CROSS'); shot('single player: campaigns')
press('CROSS'); shot('pigcam maps')
press('CROSS', 40); shot('pig01 started')
r.pad['btn'] = P['RIGHT'] | P['UP']; run(70); r.pad['btn'] = 0        # walk and turn so the map has something
press('SELECT', 6); shot('automap (SELECT)')
press('SELECT', 6)
run(30); press('START'); shot('pause menu')
press('DOWN'); press('CROSS', 20); shot('pause: game saved')
press('DOWN'); press('DOWN'); press('CROSS', 40); shot('quit to map select (saved game)')
# finish pig01 from a fresh start: pretend the exit was reached
press('SQUARE', 40)
r.wr(r.sym['level_secs'], struct.pack('<i', 95)); r.wr(r.sym['level_done'], struct.pack('<i', 2))
run(20); shot('map cleared: progress saved')
press('START', 40); shot('back at the pigcam maps')
press('CIRCLE'); shot('campaigns with progress')
press('CIRCLE'); press('DOWN'); press('CROSS'); shot('quickplay')
press('UP'); press('UP'); press('UP'); press('UP'); press('RIGHT'); press('LEFT'); press('DOWN'); press('DOWN'); press('LEFT'); shot('quickplay: CTF, 2 bots')
press('DOWN'); press('DOWN'); press('CROSS', 120); shot('CTF on Facing Cubes: warm-up'); press('UP', 40); press('R2', 90); shot('fire: the match is on')
press('START'); press('DOWN'); press('CROSS', 10); shot('quickplay: no saving'); press('DOWN'); press('DOWN'); press('CROSS', 30); shot('quit to quickplay')
press('CIRCLE'); press('DOWN'); press('CROSS'); shot('multiplayer placeholder')
press('CIRCLE'); press('DOWN'); press('DOWN'); press('CROSS', 10); shot('quit to title')
press('DOWN'); press('CROSS'); shot('load profile')
press('CROSS', 30); shot('loaded: hub')
print('profile name %r, difficulty %d' % (r.rd(r.sym['prof'] + 8, 12).split(b'\0')[0], i32('difficulty')))
cols = 3
W = Image.new('RGB', (cols * 328, ((len(shots) + cols - 1) // cols) * 262), (0, 0, 0))
d = ImageDraw.Draw(W)
for i, (tag, im) in enumerate(shots):
    x, y = (i % cols) * 328, (i // cols) * 262
    W.paste(im, (x, y)); d.text((x + 4, y + 244), tag, fill=(220, 220, 220))
W.save(sys.argv[2])
if getattr(r, 'cd_log', None) is not None:
    import collections
    print('music filters set (frame, channel):', [(f, p[1]) for f, c, p in r.cd_log if c == 0x0d][:40])
