"""Scene control for the rig: load a map, pin the camera, walk a path, collect timings and screenshots."""
import os, random, struct, sys
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import *
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from rig import Rig, REGS, FRAME

MAPS = ['pig01', 'cruel01', 'pig09', 'wsg1', 'cruel02', 'pig07', 'pigfin', 'pig02', 'pig03', 'pig04', 'pig05', 'pig08', 'wsg2', 'wsg3', 'pig06', 'fcubes',
        'xeno01', 'xeno02', 'xeno03', 'xeno04', 'xeno05', 'xeno06', 'xeno07', 'xeno08', 'xdm1', 'xdm2', 'xdm3']


class Map:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.cellsize = 12 if d[:4] == b'CMP2' else 9
        self.ss = 1 << d[4]
        nents, = struct.unpack_from('<H', d, 6)
        self.ents = [struct.unpack_from('<hhhhBBBB', d, 330 + 12 * i) for i in range(nents)]
        base = 330 + 12 * nents
        self.cells = [struct.unpack_from('<Bbb', d, base + self.cellsize * i) for i in range(self.ss * self.ss)]
        self.tags = [d[base + self.cellsize * i + 8] for i in range(self.ss * self.ss)]

    def cell(self, x, y):
        if x < 0 or y < 0 or x >= self.ss or y >= self.ss:
            return (0, 0, 0)
        return self.cells[y * self.ss + x]

    def tag(self, x, y):
        return self.tags[y * self.ss + x] if 0 <= x < self.ss and 0 <= y < self.ss else 0

    def open_cells(self, minh=5):
        out = []
        for y in range(2, self.ss - 2):
            for x in range(2, self.ss - 2):
                ok = True
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        t, f, c = self.cell(x + dx, y + dy)
                        if t == 0 or c - f < minh or abs(f - self.cell(x, y)[1]) > 1:
                            ok = False
                if ok:
                    out.append((x, y))
        return out


class Scene(Rig):
    def __init__(self, elf, cpi=1.0, files=None):
        Rig.__init__(self, elf, files or os.path.join(HERE, '..', 'maps'), cpi=cpi)
        self.pose = None
        self.pl = self.sym['pl']
        self.uc.hook_add(UC_HOOK_CODE, self._pre_render, begin=self.sym['render_frame'], end=self.sym['render_frame'])
        self.in_loop = False
        self.uc.hook_add(UC_HOOK_CODE, self._tick, begin=self.sym['game_tick'], end=self.sym['game_tick'])
        self.start()
        while not self.in_loop:
            self.run_frames(1)
        if 'menu_state' in self.sym:               # skip the front end: straight into play (ST_PLAY)
            self.wr(self.sym['menu_state'], struct.pack('<i', 2))

    def _pre_render(self, uc, addr, size, user):
        if self.pose:
            x, y, z, yaw, pitch = self.pose
            self.wr(self.pl, struct.pack('<8i', x, y, z, 0, 0, 0, yaw & 4095, pitch))
            self.wr(self.pl + 32, struct.pack('<i', 100))          # health
            self.wr(self.pl + 100, struct.pack("<i", 0))            # dead (after onfloor; ammo[10] at 56)

    def _tick(self, uc, addr, size, user):
        self.in_loop = True

    def call(self, name, *args):
        uc = self.uc
        saved = [uc.reg_read(r) for r in REGS]
        pc = self.pc
        for i, a in enumerate(args):
            uc.reg_write(REGS[4 + i], a & 0xFFFFFFFF)
        uc.reg_write(UC_MIPS_REG_RA, 0x80000100)
        sa = self.stop_after; self.stop_after = None
        uc.emu_start(self.sym[name], 0x80000100)
        v0 = uc.reg_read(UC_MIPS_REG_V0)
        for r, v in zip(REGS, saved):
            uc.reg_write(r, v)
        self.pc = pc; self.stop_after = sa
        return v0

    def load_map(self, idx):
        self.call('game_start', idx)
        name = MAPS[idx]
        self.map = Map(os.path.join(self.files_dir, name + '.cmp'))
        return self.map

    def set_pose(self, cx, cy, yaw, pitch=0, eye=819):
        t, f, c = self.map.cell(int(cx), int(cy))
        self.pose = (int(cx * 256), int(cy * 256), f * 256 + eye, yaw, pitch)

    def hold(self, n):
        """run n frames; returns (mean vblanks per frame, mean instructions, mean prims) over them"""
        f0 = len(self.frame_log)
        self.run_frames(n)
        log = self.frame_log[f0:]
        vbs = [b[0] - a[0] for a, b in zip(self.frame_log[f0 - 1:], log)]
        return (sum(vbs) / len(vbs), sum(l[1] for l in log) / len(log), sum(l[2] for l in log) / len(log))

    def spawn(self):
        for e in self.map.ents:
            if e[4] == 2:
                return e[0] + 0.5, e[1] + 0.5, (int(e[3]) * 4096 // 360) & 4095
        return 64.5, 64.5, 0
