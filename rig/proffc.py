"""profile a Facing Cubes quickplay pose at a fixed view distance.  usage: proffc.py elf dist cx cy yaw pitch [bots]"""
import sys, struct
sys.path.insert(0, '.')
from scene import *
elf = sys.argv[1]; d = int(sys.argv[2]); cx, cy, yaw, pitch = (float(v) for v in sys.argv[3:7]); nb = int(sys.argv[7]) if len(sys.argv) > 7 else 7
s = Scene(elf); s.call('game_start_quick', 15, 3, nb); s.call('game_quick_begin'); s.map = Map(os.path.join(s.files_dir, 'fcubes.cmp')); s.hold(30)
s.wr(s.sym['dbg_flags'], struct.pack('<i', 8)); s.wr(s.sym['dbg_far'], struct.pack('<i', d))
s.set_pose(cx, cy, int(yaw), int(pitch)); s.hold(2)
s.prof = {}; s.prof_on = True; vb, ins, pr = s.hold(4); s.prof_on = False
tot = sum(s.prof.values())
print('total/frame', tot // 4, 'prims', pr)
for k, v in sorted(s.prof.items(), key=lambda kv: -kv[1])[:22]: print('%9d %5.1f%% %s' % (v // 4, 100 * v / tot, k))
if len(sys.argv) > 8: s.screenshot(sys.argv[8])
