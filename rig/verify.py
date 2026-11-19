#!/usr/bin/env python3
"""Compare the real renderer with a brute-force reference (no occlusion culling, no merged squares) on random poses.
usage: verify.py elf [poses per map] [maps]"""
import sys, random
sys.path.insert(0, '.')
from scene import *
from PIL import Image, ImageChops
elf = sys.argv[1]; N = int(sys.argv[2]) if len(sys.argv) > 2 else 6
maps = [int(v) for v in sys.argv[3].split(',')] if len(sys.argv) > 3 else [0, 1, 2, 3]
REF = int(sys.argv[4]) if len(sys.argv) > 4 else 2
s = Scene(elf)
res = []
for mi in maps:
    m = s.load_map(mi); cells = m.open_cells(); rnd = random.Random(99 + mi)
    for k in range(N):
        cx, cy = rnd.choice(cells); yaw = rnd.randrange(4096); pitch = rnd.choice([0, 0, 0, 200, -200, 500, -500])
        s.set_pose(cx + 0.5, cy + 0.5, yaw, pitch)
        s.wr(s.sym['dbg_flags'], struct.pack('<i', 8 | 16)); s.hold(2)
        a = s.screenshot(None, 1).crop((48, 24, 272, 160)); pa = s.last_prims
        s.wr(s.sym['dbg_flags'], struct.pack('<i', 8 | 16 | REF)); s.hold(2)
        b = s.screenshot(None, 1).crop((48, 24, 272, 160))
        d = ImageChops.difference(a, b).convert('L')
        bad = sum(1 for v in d.tobytes() if v > 40)
        res.append((bad, mi, cx, cy, yaw, pitch, a, b, pa, s.last_prims))
        print('map %d pos %3d,%3d yaw %4d pitch %4d : %5d px differ  prims %d vs %d' % (mi, cx, cy, yaw, pitch, bad, pa, s.last_prims), flush=True)
res.sort(key=lambda r: -r[0])
W = Image.new('RGB', (448, 136 * 4))
for i, r in enumerate(res[:4]):
    W.paste(r[6], (0, i * 136)); W.paste(r[7], (224, i * 136))
W.resize((896, 136 * 8), Image.NEAREST).save('/tmp/verify.png')
print('worst:', [(r[0], r[1], r[2], r[3], r[4], r[5]) for r in res[:4]])
