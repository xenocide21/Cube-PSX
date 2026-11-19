#!/usr/bin/env python3
"""Benchmark: random standing poses on every map.  usage: bench.py elf [poses per map] [flags]
Reports instructions per frame (the rig's time unit: 564,480 per vblank, so 1.13M = 30 fps)."""
import sys, random
sys.path.insert(0, '.')
from scene import *
elf = sys.argv[1]; N = int(sys.argv[2]) if len(sys.argv) > 2 else 12; flags = int(sys.argv[3]) if len(sys.argv) > 3 else 8
maps = [int(v) for v in sys.argv[4].split(',')] if len(sys.argv) > 4 else [0, 1, 2, 3]
s = Scene(elf)
rows = []
for mi in maps:
    m = s.load_map(mi); cells = m.open_cells(); rnd = random.Random(1234 + mi)
    if 'dbg_flags' in s.sym: s.wr(s.sym['dbg_flags'], struct.pack('<i', flags))
    for k in range(N):
        cx, cy = rnd.choice(cells); yaw = rnd.randrange(4096); pitch = rnd.choice([0, 0, 0, 150, -150, 400, -400])
        s.set_pose(cx + 0.5, cy + 0.5, yaw, pitch); s.hold(2)
        vb, ins, pr = s.hold(2)
        rows.append((ins, pr, mi, cx, cy, yaw, pitch, s.textout.get(0, '').split('\n')[-2]))
rows.sort()
n = len(rows)
print('poses %d  insns: median %d  p90 %d  max %d   prims: median %d max %d' % (n, rows[n // 2][0], rows[n * 9 // 10][0], rows[-1][0], sorted(r[1] for r in rows)[n // 2], max(r[1] for r in rows)))
print('over 2 vblanks (1.13M): %d   over 1.0M: %d' % (sum(1 for r in rows if r[0] > 1129000), sum(1 for r in rows if r[0] > 1000000)))
for r in rows[n // 2 - 1:n // 2 + 2] + rows[-6:]: print('  %8d insns %4d prims map %d pos %d,%d yaw %d pitch %d  %s' % r)
