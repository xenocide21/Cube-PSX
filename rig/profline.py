"""line-level profile of one function.  usage: profline.py elf func map cx cy yaw pitch"""
import sys, subprocess, collections
sys.path.insert(0, '.')
from scene import *
from unicorn import UC_HOOK_BLOCK
elf, fn = sys.argv[1], sys.argv[2]; mi, cx, cy, yaw, pitch = (int(v) for v in sys.argv[3:8])
s = Scene(elf)
_l = [l for l in subprocess.run(['mipsel-linux-gnu-nm', '-S', elf], capture_output=True, text=True).stdout.split('\n') if l.endswith(' t ' + fn) or l.endswith(' T ' + fn)][0].split()
lo, sz = int(_l[0], 16), int(_l[1], 16)
s.load_map(mi); s.set_pose(cx + .5, cy + .5, yaw, pitch); s.hold(2)
cnt = collections.Counter()
def hk(uc, a, size, u):
    if lo <= a < lo + sz:
        for k in range(0, size, 4): cnt[a + k] += 1
h = s.uc.hook_add(UC_HOOK_BLOCK, hk); s.uc.ctl_flush_tb(); s.hold(4); s.uc.hook_del(h)
addrs = sorted(cnt)
out = subprocess.run(['mipsel-linux-gnu-addr2line', '-e', elf, '-i'] + ['%x' % a for a in addrs], capture_output=True, text=True).stdout.split('\n')
# -i may print several lines per address; use without -i for simplicity
out = subprocess.run(['mipsel-linux-gnu-addr2line', '-e', elf] + ['%x' % a for a in addrs], capture_output=True, text=True).stdout.split('\n')
byline = collections.Counter()
for a, l in zip(addrs, out): byline[l.split('/')[-1]] += cnt[a]
tot = sum(byline.values())
print('total', tot // 4)
for k, v in byline.most_common(25): print('%8d %s' % (v // 4, k))
