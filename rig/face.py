#!/usr/bin/env python3
"""Portrait check: new game from the title menu, then poke the player's health / hurt / kills / dead and photograph the
status bar.  usage: face.py elf out.png"""
import sys, struct
sys.path.insert(0, '.')
from rig import Rig
from PIL import Image
P = dict(UP=16, DOWN=64, START=8, CROSS=16384)
r = Rig(sys.argv[1], '../maps'); r.start()
pl = r.sym['pl']
shots = []
def run(n): r.run_frames(n)
def press(b, after=4):
    r.pad['btn'] = P[b]; run(1); r.pad['btn'] = 0; run(after)
def state(): return struct.unpack('<i', r.rd(r.sym['menu_state'], 4))[0]
def poke(off, v): r.wr(pl + off, struct.pack('<i', v))
def peek(off): return struct.unpack('<i', r.rd(pl + off, 4))[0]
def shot(tag):
    im = r.screenshot(None, 1).crop((0, 0, 320, 240))
    shots.append((tag, im)); print('%-24s health %3d hurt %2d kills %d dead %d' % (tag, peek(32), peek(104), peek(108), peek(100)))
run(20); press('CROSS', 2)
while state() == 0: run(1)
run(60); press('CROSS', 40)                      # NEW GAME
full = r.screenshot(None, 1).crop((0, 0, 320, 240))
for i in range(12):                              # idle: glances, blinks, ember, smoke
    run(8 if 4 <= i < 9 else 18); shot('idle %d' % i)
    f = struct.unpack('<17i', r.rd(r.sym['fc'], 68)); print('   fc look %d until %d  blink %d  puff_at %d puff_until %d  smoke st %s  vb %d' % (f[0], f[1], f[2], f[6], f[7], f[14:17], r.frame_log[-1][0]))
for hp in (60, 40, 15):
    poke(32, hp); run(2); shot('health %d' % hp)
poke(32, 90); poke(104, 18); run(1); shot('hit')
run(14); shot('hit, recovering')
poke(108, peek(108) + 1); run(3); shot('kill: smirk')
poke(100, 1); run(2); shot('dead')
cols = 4
W = Image.new('RGB', (320 + 8 + cols * 168, max(240, ((len(shots) + cols - 1) // cols) * 172)), (0, 0, 0))
W.paste(full, (0, 0))
for i, (tag, im) in enumerate(shots):
    W.paste(im.crop((0, 184, 56, 240)).resize((168, 168), Image.NEAREST), (328 + (i % cols) * 168, (i // cols) * 172))
W.save(sys.argv[2])
