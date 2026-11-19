#!/usr/bin/env python3
"""Audio check: boot, go to the title screen and let the attract demo play, then mix everything the SPU was told to play
into a WAV and list which sounds played.  usage: audio.py elf out.wav [seconds]"""
import sys, struct
sys.path.insert(0, '.')
from rig import Rig
import os

NAMES = ['jump', 'land', 'rifle', 'punch', 'shotgun', 'chaingun', 'rocket fire', 'rocket hit', 'weapon load', 'ammo', 'health',
         'armour', 'powerup', 'item spawn', 'teleport', 'no ammo', 'powerup out', 'pain1', 'pain2', 'pain3', 'pain4', 'pain5',
         'player pain', 'die1', 'die2', 'fireball launch', 'fireball burst', 'splash out', 'splash in', 'grunt1', 'grunt2', 'door rumble',
         'ogre pain', 'rhino pain', 'rhino death', 'ratamahatta pain', 'ratamahatta death', 'slith pain', 'slith death', 'bauul pain',
         'bauul death', 'hellpig pain', 'hellpig death', 'knight pain', 'knight death', 'goblin pain', 'goblin death', 'bite',
         'iceball', 'slimeball', 'jump pad']

r = Rig(sys.argv[1], '../maps'); r.start()
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 30
addr2id = {}; base = 0x1010; first = 0
for f in ('sound.pak', 'voice.pak'):
    pak = open('/home/claude/cube_ps1/maps/' + f, 'rb').read(); n = struct.unpack_from('<H', pak, 4)[0]
    size = 0
    for i in range(n):
        off, sz = struct.unpack_from('<II', pak, 8 + 12 * i); addr2id[base + off] = first + i; size = max(size, off + sz)
    base += (size + 63) & ~63; first += n
r.run_frames(20); r.pad['btn'] = 16384; r.run_frames(1); r.pad['btn'] = 0       # X: skip the splash
st = lambda: struct.unpack('<i', r.rd(r.sym['menu_state'], 4))[0]
while st() == 0: r.run_frames(1)
t0 = r.cycles() / r.FRAME_CYC
r.run_frames(int(secs * 30))
t1 = r.cycles() / r.FRAME_CYC
ons = [e for e in r.spu.keyons() if t0 <= e[0] <= t1]
from collections import Counter
c = Counter(NAMES[addr2id.get(e[3], -1)] if e[3] in addr2id else 'unknown %x' % e[3] for e in ons)
print('%.1f s of attract demo: %d sounds started' % ((t1 - t0) / 60, len(ons)))
for k, v in c.most_common(): print('  %-20s %d' % (k, v))
peak = r.spu.render(sys.argv[2], t0, t1)
print('mixed to %s (peak %d of 32767, %.2f%% of samples clipped)' % (sys.argv[2], peak, 100 * r.spu.clipped))
