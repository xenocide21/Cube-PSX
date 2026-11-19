#!/usr/bin/env python3
"""View-distance sweep on Facing Cubes: quickplay CTF with 7 bots, fixed view distances, worst-case poses.
usage: farsweep.py elf [dist,dist,...]   (rig time unit: instructions; 1.13M a frame = 30 fps)"""
import sys, struct, random
sys.path.insert(0, '.')
from scene import *
elf = sys.argv[1]
dists = [int(v) for v in sys.argv[2].split(',')] if len(sys.argv) > 2 else [20, 32, 44, 56, 72, 88, 104]
s = Scene(elf)
s.call('game_start_quick', 15, 3, 7); s.call('game_quick_begin')
s.map = Map(os.path.join(s.files_dir, 'fcubes.cmp'))
s.hold(30)                       # let the bots spread out a little
m = s.map; rnd = random.Random(5)
# named spots (cell x, y, yaw, pitch): yaw 0 = +y, 1024 = -x, 2048 = -y, 3072 = +x
spots = [('red door, across the map', 38.5, 64.5, 3072, 0), ('blue door, across', 89.5, 63.5, 1024, 0),
         ('middle platform, to red', 64.5, 64.5, 1024, 0), ('middle platform, to blue', 64.5, 64.5, 3072, 0),
         ('middle, along y', 64.5, 64.5, 0, 0), ('red roof, across', 30.5, 50.5, 3072, -150), ('blue roof, across', 97.5, 77.5, 1024, -150)]
cells = [c for c in m.open_cells() if m.cell(*c)[1] > -50]      # (not down in the abyss: you die there)
for k in range(10):
    cx, cy = rnd.choice(cells); spots.append(('random %d' % k, cx + 0.5, cy + 0.5, rnd.randrange(4096), rnd.choice([0, 0, 150, -150])))
s.wr(s.sym['dbg_flags'], struct.pack('<i', 8))
print('%-28s' % 'spot' + ''.join('%9d' % d for d in dists))
worst = [0] * len(dists); prim = [0] * len(dists); tot = [0] * len(dists)
for name, x, y, yaw, pitch in spots:
    row = []
    for i, d in enumerate(dists):
        s.wr(s.sym['dbg_far'], struct.pack('<i', d))
        s.set_pose(x, y, yaw, pitch); s.hold(2)
        vb, ins, pr = s.hold(2)
        row.append(ins); worst[i] = max(worst[i], ins); prim[i] = max(prim[i], pr); tot[i] += ins
    print('%-28s' % name + ''.join('%8.2fM' % (v / 1e6) for v in row))
print('%-28s' % 'WORST' + ''.join('%8.2fM' % (v / 1e6) for v in worst))
print('%-28s' % 'mean' + ''.join('%8.2fM' % (v / len(spots) / 1e6) for v in tot))
print('%-28s' % 'max prims' + ''.join('%9d' % v for v in prim))
