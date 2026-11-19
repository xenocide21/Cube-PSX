"""per-function instruction profile at one pose.  usage: prof.py elf map cx cy yaw pitch"""
import sys
sys.path.insert(0, '.')
from scene import *
elf = sys.argv[1]; mi, cx, cy, yaw, pitch = (int(v) for v in sys.argv[2:7])
s = Scene(elf); s.load_map(mi); s.set_pose(cx + .5, cy + .5, yaw, pitch); s.hold(2)
s.prof = {}; s.prof_on = True; s.hold(4); s.prof_on = False
tot = sum(s.prof.values())
print('total/frame', tot // 4)
for k, v in sorted(s.prof.items(), key=lambda kv: -kv[1])[:25]: print('%9d %5.1f%% %s' % (v // 4, 100 * v / tot, k))
if 'node_count' in s.sym: print('node_count', struct.unpack('<i', s.rd(s.sym['node_count'], 4))[0])
