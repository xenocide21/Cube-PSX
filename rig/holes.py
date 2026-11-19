#!/usr/bin/env python3
"""Hole hunt: the view is pre-filled with magenta; any magenta left after a frame is a pixel nothing drew.
Poses include standing right against walls and looking steeply up/down.  usage: holes.py elf [poses per map] [maps]"""
import sys, random
sys.path.insert(0, '.')
from scene import *
from PIL import Image
elf = sys.argv[1]; N = int(sys.argv[2]) if len(sys.argv) > 2 else 20
maps = [int(v) for v in sys.argv[3].split(',')] if len(sys.argv) > 3 else [0, 1, 2, 3]
s = Scene(elf); s.hole_fill = (0, 0, 320, 184)
bad = []; tot = 0; dropped = 0
for mi in maps:
    m = s.load_map(mi); rnd = random.Random(7 + mi)
    def ok(x, y, fx, fy):                      # a place the player can really stand: nothing solid within the 1.1 unit body radius
        if m.cell(x, y)[0] != 4 or m.cell(x, y)[2] - m.cell(x, y)[1] < 4 or m.tag(x, y): return False
        for cy in range(int(y + fy - 1.1), int(y + fy + 1.1) + 1):
            for cx in range(int(x + fx - 1.1), int(x + fx + 1.1) + 1):
                t, f, c = m.cell(cx, cy)
                if t in (0, 1) or f > m.cell(x, y)[1] + 1 or m.tag(cx, cy): return False   # doors start closed
        return True
    cells = [(x, y) for y in range(2, m.ss - 2) for x in range(2, m.ss - 2) if m.cell(x, y)[0] == 4]
    s.wr(s.sym['dbg_flags'], struct.pack('<i', 32 | 16))
    for k in range(N):
        while True:
            cx, cy = rnd.choice(cells); fx = rnd.choice([0.5, 0.11, 0.89, 0.3]); fy = rnd.choice([0.5, 0.11, 0.89, 0.7])
            if ok(cx, cy, fx, fy): break
        px = cx + fx; py = cy + fy
        yaw = rnd.randrange(4096); pitch = rnd.choice([0, 0, 0, 0, 200, -200, 600, -600, 900, -900])
        s.set_pose(px, py, yaw, pitch); s.hold(1)
        im = s.screenshot(None, 1).crop((0, 0, 320, 184))
        n = sum(1 for p in im.getdata() if p == (248, 0, 248))
        tot += 1; dropped += s.last_dropped
        if n or s.last_dropped: bad.append((n, s.last_dropped, mi, px, py, yaw, pitch, im))
bad.sort(key=lambda b: -b[0])
print('poses %d, with unpainted pixels or GPU-dropped polygons: %d, dropped polygons total %d' % (tot, len(bad), dropped))
for b in bad[:12]: print('  %5d px, %d dropped: map %d pos %.2f,%.2f yaw %d pitch %d' % b[:7])
if bad:
    W = Image.new('RGB', (640, 184 * 2))
    for i, b in enumerate(bad[:4]): W.paste(b[7], ((i % 2) * 320, (i // 2) * 184))
    W.save('/tmp/holes.png')
