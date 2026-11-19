#!/usr/bin/env python3
"""Timed walk: the camera tours the map along a path while the game runs (monsters awake, real frame pacing and the
adaptive view distance).  Reports the frame-time histogram the way the console would see it.
usage: walk.py elf map [frames] [cpi]"""
import sys, random, math, collections
sys.path.insert(0, '.')
from scene import *
elf = sys.argv[1]; mi = int(sys.argv[2]); NF = int(sys.argv[3]) if len(sys.argv) > 3 else 400; cpi = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
s = Scene(elf, cpi=cpi); m = s.load_map(mi); rnd = random.Random(5)
for t in sorted(set(m.tags) - {0}):                 # every door open, so the walk can tour the whole map
    s.call('world_settag', t, 4)
    for k, tg in enumerate(m.tags):
        if tg == t: m.cells[k] = (4,) + tuple(m.cells[k][1:])
def walkable(x, y):
    t, f, c = m.cell(x, y)
    return t == 4 and c - f >= 4
def path(a, b):
    prev = {a: None}; q = collections.deque([a])
    while q:
        c = q.popleft()
        if c == b: break
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + dx, c[1] + dy)
            if n not in prev and walkable(*n) and abs(m.cell(*n)[1] - m.cell(*c)[1]) <= 2: prev[n] = c; q.append(n)
    if b not in prev: return list(prev) if b == (-1, -1) else None
    out = []
    while b: out.append(b); b = prev[b]
    return out[::-1]
sx, sy, yaw = s.spawn(); cur = (int(sx), int(sy)); pts = []
reach = path(cur, (-1, -1)) or []                # (no such target: the search floods everything reachable from the spawn)
rs = set(reach); cells = [c for c in m.open_cells() if c in rs]
if len(cells) < 20: cells = reach               # a small closed start room: tour what there is
tries = 0
while len(pts) < NF * 0.2 + 50 and tries < 400:
    tries += 1; p = path(cur, rnd.choice(cells))
    if p and len(p) > 3: pts += p; cur = p[-1]
x, y = pts[0][0] + 0.5, pts[0][1] + 0.5; i = 1; f0 = len(s.frame_log); yawf = float(yaw); vbs = []; fars = []; worst = []
for f in range(NF):
    tx, ty = pts[min(i, len(pts) - 1)][0] + 0.5, pts[min(i, len(pts) - 1)][1] + 0.5
    d = math.hypot(tx - x, ty - y)
    if d < 0.2: i += 1
    else: x += (tx - x) / d * 0.16; y += (ty - y) / d * 0.16
    want = math.atan2(-(tx - x), (ty - y)) / (2 * math.pi) * 4096 + 300 * math.sin(f / 23.0)
    dy_ = ((want - yawf + 2048) % 4096) - 2048; yawf += max(-60, min(60, dy_))
    s.set_pose(x, y, int(yawf), int(120 * math.sin(f / 31.0)))
    before = s.frame_log[-1][0]; s.run_frames(1); fl = s.frame_log[-1]
    vbs.append(fl[0] - before); txt = s.textout.get(0, '').split('\n')[-2]
    fars.append(int(txt.split()[1]) if txt.startswith('VIEW') else 0)
    worst.append((fl[1], f, round(x, 1), round(y, 1), int(yawf) & 4095, txt))
h = collections.Counter(vbs)
print('map %d: %d frames, vblanks/frame histogram %s' % (mi, NF, dict(sorted(h.items()))))
print('  30 fps or better: %.1f%%   average fps %.1f   view distance min %d median %d' % (100.0 * sum(v for k, v in h.items() if k <= 2) / NF, 60.0 * NF / sum(vbs), min(fars[20:]), sorted(fars)[NF // 2]))
ins = sorted(w[0] for w in worst)
print('  instructions/frame: median %d  p95 %d  max %d' % (ins[NF // 2], ins[NF * 95 // 100], ins[-1]))
for w in sorted(worst)[-3:]: print('   ', w)
