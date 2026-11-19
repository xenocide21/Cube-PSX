#!/usr/bin/env python3
"""Cube PSX test rig: runs the real PS1 executable (cubepsx.elf) on an emulated R3000 (Unicorn) with a modelled GTE,
high-level stand-ins for the SDK's hardware functions, and a software GPU, so frames can be counted and looked at
without a console.  Time is modelled as instructions x CPI plus GTE command costs; VSync()/hblank timers follow it."""
import bisect, ctypes, os, struct, subprocess, sys
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN, UC_HOOK_CODE, UC_HOOK_BLOCK, UC_HOOK_MEM_UNMAPPED
from unicorn.mips_const import *
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gte import GTE

FRAME = 33868800 // 60                 # CPU cycles per vblank (NTSC)
HBL = FRAME / 263.0
GTE_COST = {0x01: 15, 0x30: 23, 0x06: 8, 0x2D: 5, 0x2E: 6, 0x12: 8, 0x28: 5, 0x0C: 6}
REGS = [UC_MIPS_REG_0 + i for i in range(32)]
A0, A1, A2, A3, V0, SP, RA, GP = (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_V0, UC_MIPS_REG_SP, UC_MIPS_REG_RA, UC_MIPS_REG_GP)


def build_rast():
    so = os.path.join(HERE, 'psxrast.so')
    src = os.path.join(HERE, 'psxrast.c')
    if not os.path.exists(so) or os.path.getmtime(so) < os.path.getmtime(src):
        subprocess.check_call(['gcc', '-O2', '-shared', '-fPIC', '-o', so, src])
    lib = ctypes.CDLL(so)
    lib.rast_vram.restype = ctypes.POINTER(ctypes.c_uint16)
    lib.rast_stat.restype = ctypes.c_long
    lib.rast_last_drop.restype = ctypes.POINTER(ctypes.c_int)
    return lib


class Rig:
    def __init__(self, elf, files_dir, cpi=1.0, shots=None):
        self.cpi = cpi
        self.files_dir = files_dir
        self.shots = shots
        self.rast = build_rast()
        self.vram = self.rast.rast_vram()
        self.gte = GTE()
        self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_LITTLE_ENDIAN)
        uc = self.uc
        uc.mem_map(0x00000000, 0x200000)
        uc.mem_map(0x1F800000, 0x1000)
        self.base = 0x80000000
        data = open(elf, 'rb').read()
        self.load_elf(data)
        self.load_syms(elf)
        for a in (0xA0, 0xB0, 0xC0):
            uc.mem_write(a, struct.pack('<II', 0x03E00008, 0))
        self.insns = 0
        self.extra = 0.0                     # cycles added by waits and GTE commands
        self.last_hblank = 0
        self.frames = 0
        self.frame_log = []                  # (vblank at flip, instructions in frame, prims, pixels)
        self.prof = {}
        self.text = {}
        self.textout = {}
        self.draw_env = (0, 0, 320, 240, 0, 0)
        self.cdfiles = []
        self.pad_buf = 0
        self.pad = dict(btn=0, lx=128, ly=128, rx=128, ry=128)
        self.on_frame = None
        self.stop_after = None
        self.last_insns = 0
        self.resume_skip = False
        self.hole_fill = None
        self.card = {}; self.fds = {}
        self.keep_prims = None
        self.prof_on = True
        self.patch_cop2()
        self.install_hle()
        from spu import SPU
        self.FRAME_CYC = FRAME
        self.spu = SPU(self)
        uc.hook_add(UC_HOOK_BLOCK, self.on_block)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self.on_unmapped)

    # ---- loading ----
    def load_elf(self, data):
        phoff, = struct.unpack_from('<I', data, 28)
        phentsize, phnum = struct.unpack_from('<HH', data, 42)
        self.entry, = struct.unpack_from('<I', data, 24)
        self.text_ranges = []
        for i in range(phnum):
            p_type, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from('<8I', data, phoff + i * phentsize)
            if p_type != 1:
                continue
            self.uc.mem_write(vaddr & 0x1FFFFFFF, data[off:off + filesz])
        shoff, = struct.unpack_from('<I', data, 32)
        shentsize, shnum, shstr = struct.unpack_from('<HHH', data, 46)
        for i in range(shnum):
            name, typ, flags, addr, off, size = struct.unpack_from('<6I', data, shoff + i * shentsize)
            if typ == 1 and (flags & 4) and size:
                self.text_ranges.append((addr, size))

    def load_syms(self, elf):
        out = subprocess.check_output(['mipsel-linux-gnu-nm', '-n', elf]).decode()
        self.sym = {}
        funcs = []
        for line in out.splitlines():
            parts = line.split()
            if len(parts) != 3:
                continue
            addr = int(parts[0], 16)
            self.sym[parts[2]] = addr
            if parts[1] in 'Tt' and not parts[2].startswith('.'):
                funcs.append((addr, parts[2]))
        funcs.sort()
        self.faddr = [a for a, _ in funcs]
        self.fname = [n for _, n in funcs]

    def rd(self, addr, n):
        return bytes(self.uc.mem_read(addr & 0x1FFFFFFF, n))

    def wr(self, addr, b):
        self.uc.mem_write(addr & 0x1FFFFFFF, bytes(b))

    def r32(self, addr):
        return struct.unpack('<I', self.rd(addr, 4))[0]

    def cstr(self, addr):
        s = b''
        while True:
            c = self.rd(addr, 1)
            if c == b'\0' or len(s) > 200:
                return s.decode('latin1')
            s += c; addr += 1

    # ---- GTE: every COP2 instruction is replaced by a NOP and modelled from a hook at its address ----
    def patch_cop2(self):
        self.cop2 = {}
        for addr, size in self.text_ranges:
            code = self.rd(addr, size)
            for i in range(0, size - 3, 4):
                w, = struct.unpack_from('<I', code, i)
                op = w >> 26
                if op in (0x12, 0x32, 0x3A):
                    self.cop2[addr + i] = w
                    self.wr(addr + i, b'\0\0\0\0')
        for a in self.cop2:
            self.uc.hook_add(UC_HOOK_CODE, self.on_cop2, begin=a, end=a)

    def on_cop2(self, uc, addr, size, user):
        w = self.cop2[addr]
        op = w >> 26
        g = self.gte
        rt = (w >> 16) & 31; rd = (w >> 11) & 31
        if op == 0x12:
            if w & (1 << 25):
                g.command(w & 0x1FFFFFF)
                self.extra += GTE_COST.get(w & 0x3F, 8)
            else:
                rs = (w >> 21) & 31
                if rs == 0: uc.reg_write(REGS[rt], g.read_data(rd))
                elif rs == 2: uc.reg_write(REGS[rt], g.read_ctrl(rd))
                elif rs == 4: g.write_data(rd, uc.reg_read(REGS[rt]))
                elif rs == 6: g.write_ctrl(rd, uc.reg_read(REGS[rt]))
                else: raise RuntimeError('cop2 form %08x at %08x' % (w, addr))
        else:
            base = uc.reg_read(REGS[(w >> 21) & 31])
            ea = (base + ((w & 0xFFFF) - 0x10000 if w & 0x8000 else w & 0xFFFF)) & 0xFFFFFFFF
            if op == 0x32:
                g.write_data(rt, self.r32(ea))
            else:
                self.wr(ea, struct.pack('<I', g.read_data(rt)))

    # ---- timing ----
    def cycles(self):
        return self.insns * self.cpi + self.extra

    def on_block(self, uc, addr, size, user):
        n = size >> 2
        self.insns += n
        if self.prof_on:
            i = bisect.bisect_right(self.faddr, addr) - 1
            name = self.fname[i] if i >= 0 else '?'
            self.prof[name] = self.prof.get(name, 0) + n

    def on_unmapped(self, uc, access, addr, size, value, user):
        pc = uc.reg_read(UC_MIPS_REG_PC)
        i = bisect.bisect_right(self.faddr, pc) - 1
        print('UNMAPPED access %08x at pc %08x (%s)' % (addr, pc, self.fname[i] if i >= 0 else '?'))
        return False

    # ---- high-level SDK stand-ins ----
    def install_hle(self):
        names = ['ResetGraph', 'PutDrawEnv', 'PutDispEnv', 'SetDispMask', 'DrawOTag', 'DrawSync', 'VSync', 'LoadImage', 'ClearOTagR',
                 'FntLoad', 'FntOpen', 'FntPrint', 'FntFlush', 'InitGeom', 'CdInit', 'CdSearchFile', 'CdControl', 'CdRead', 'CdReadSync',
                 'InitPAD', 'StartPAD', 'ChangeClearPAD', 'printf', 'puts', 'SpuInit',
                 'InitCARD', 'StartCARD', '_bu_init', 'open', 'close', 'read', 'write']
        self.hle_range = []
        for n in names:
            if n not in self.sym:
                continue
            a = self.sym[n]
            self.wr(a, struct.pack('<II', 0x03E00008, 0))
            self.cop2.pop(a, None); self.cop2.pop(a + 4, None)
            self.uc.hook_add(UC_HOOK_CODE, getattr(self, 'h_' + n), begin=a, end=a)

    def arg(self, i):
        return self.uc.reg_read((A0, A1, A2, A3)[i])

    def ret(self, v):
        self.uc.reg_write(V0, v & 0xFFFFFFFF)

    def h_ResetGraph(self, *a): pass
    def h_SetDispMask(self, *a): pass
    def h_FntLoad(self, *a): pass
    def h_StartPAD(self, *a): pass
    def h_ChangeClearPAD(self, *a): pass
    def h_printf(self, *a): pass
    def h_puts(self, *a): pass
    def h_SpuInit(self, *a): pass
    def h_CdInit(self, *a): self.ret(1)
    # memory card: BIOS file calls on bu00:, kept in self.card {name: bytearray}; set self.card = None for "no card"
    def h_InitCARD(self, *a): pass
    def h_StartCARD(self, *a): pass
    def h__bu_init(self, *a): pass
    def h_open(self, *a):
        name = self.cstr(self.arg(0)); mode = self.arg(1)
        if self.card is None or not name.startswith('bu00:'): self.ret(-1); return
        name = name[5:]
        if mode & 0x200:
            if name in self.card: self.ret(-1); return
            self.card[name] = bytearray((mode >> 16) * 8192)
        elif name not in self.card: self.ret(-1); return
        fd = len(self.fds) + 3; self.fds[fd] = [name, 0]; self.ret(fd)
    def h_close(self, *a): self.fds.pop(self.arg(0), None); self.ret(0)
    def h_read(self, *a):
        f = self.fds.get(self.arg(0)); n = self.arg(2)
        if not f or n % 128: self.ret(-1); return
        data = self.card[f[0]][f[1]:f[1] + n]; self.wr(self.arg(1), bytes(data)); f[1] += len(data); self.ret(len(data))
    def h_write(self, *a):
        f = self.fds.get(self.arg(0)); n = self.arg(2)
        if not f or n % 128: self.ret(-1); return
        buf = self.card[f[0]]; n = min(n, len(buf) - f[1]); buf[f[1]:f[1] + n] = self.rd(self.arg(1), n); f[1] += n; self.ret(n)
    def h_DrawSync(self, *a): self.ret(0)
    def h_FntOpen(self, *a): self.ret(len(self.text)); self.text[len(self.text)] = ''

    def h_InitGeom(self, *a):
        self.gte.write_ctrl(26, 320); self.gte.write_ctrl(29, 341); self.gte.write_ctrl(30, 256)

    def h_InitPAD(self, *a):
        self.pad_buf = self.arg(0)
        self.write_pad()

    def write_pad(self):
        if not self.pad_buf:
            return
        p = self.pad
        self.wr(self.pad_buf, struct.pack('<BBHBBBB', 0, 0x73, (~p['btn']) & 0xFFFF, p['rx'], p['ry'], p['lx'], p['ly']))

    def h_PutDispEnv(self, *a):
        x, y, w, h = struct.unpack('<4h', self.rd(self.arg(0), 8))
        self.disp = (x, y, w, h)

    def h_PutDrawEnv(self, *a):
        e = self.rd(self.arg(0), 28)
        x, y, w, h, ox, oy = struct.unpack_from('<6h', e, 0)
        isbg, r, g, b = e[24], e[25], e[26], e[27]
        self.draw_env = (x, y, w, h, x + ox, y + oy)
        self.rast.rast_set_env(x, y, x + w - 1, y + h - 1, x + ox, y + oy)
        if isbg:
            self.rast.rast_fill(x, y, w, h, r, g, b)
        if self.hole_fill:
            self.rast.rast_fill(x + self.hole_fill[0], y + self.hole_fill[1], self.hole_fill[2], self.hole_fill[3], 255, 0, 255)

    def h_LoadImage(self, *a):
        x, y, w, h = struct.unpack('<4h', self.rd(self.arg(0), 8))
        data = self.rd(self.arg(1), w * h * 2)
        px = struct.unpack('<%dH' % (w * h), data)
        for j in range(h):
            row = (y + j) * 1024 + x
            for i in range(w):
                self.vram[row + i] = px[j * w + i]
        self.ret(0)

    def h_ClearOTagR(self, *a):
        ot, n = self.arg(0), self.arg(1)
        words = [0x00FFFFFF] + [((ot + 4 * (i - 1)) & 0xFFFFFF) for i in range(1, n)]
        self.wr(ot, struct.pack('<%dI' % n, *words))
        self.extra += n            # DMA OT clear: about a cycle a word

    def h_DrawOTag(self, *a):
        if self.resume_skip:
            self.resume_skip = False; return
        addr = self.arg(0)
        self.rast.rast_reset_stats()
        nprim = 0; words_total = 0; guard = 0
        if self.keep_prims is not None: self.keep_prims = []
        while True:
            tag = self.r32(addr)
            n = tag >> 24
            if n:
                buf = self.rd(addr + 4, n * 4)
                arr = (ctypes.c_uint32 * n).from_buffer_copy(buf)
                if self.keep_prims is not None: self.keep_prims.append(struct.unpack('<%dI' % n, buf))
                if not self.rast.rast_gp0(arr, n):
                    print('unknown GP0 packet %08x at %08x' % (arr[0], addr))
                nprim += 1; words_total += n
            nxt = tag & 0xFFFFFF
            guard += 1
            if nxt == 0xFFFFFF or guard > 200000:
                break
            addr = 0x80000000 | nxt
        self.last_prims = nprim
        self.last_words = words_total
        self.last_pixels = self.rast.rast_stat(0)
        self.last_dropped = self.rast.rast_stat(1)
        if self.last_dropped:
            d = self.rast.rast_last_drop(); self.drop_info = [d[i] for i in range(8)]
        self.frames += 1
        vb = int(self.cycles() // FRAME)
        self.frame_log.append((vb, self.insns - self.last_insns, nprim, self.last_pixels, self.last_dropped))
        self.last_insns = self.insns
        if self.shots is not None:
            self.shots(self)
        if self.on_frame:
            self.on_frame(self)
        self.write_pad()
        if self.stop_after is not None and self.frames >= self.stop_after:
            self.uc.emu_stop()

    def h_VSync(self, *a):
        mode = self.arg(0)
        if mode >= 0x80000000:
            self.ret(int(self.cycles() // FRAME)); return
        now_h = int(self.cycles() / HBL)
        delta = (now_h - self.last_hblank) & 0xFFFF
        if mode == 1:
            self.ret(delta); return
        vb = int(self.cycles() // FRAME) + 1
        self.extra += vb * FRAME - self.cycles()
        self.last_hblank = int(self.cycles() / HBL)
        self.ret(delta)

    def h_FntPrint(self, *a):
        uc = self.uc
        sid = self.arg(0); fmt = self.cstr(self.arg(1))
        sp = uc.reg_read(SP)
        args = [self.arg(2), self.arg(3)] + [self.r32(sp + 16 + 4 * i) for i in range(8)]
        out = ''; i = 0; k = 0
        while i < len(fmt):
            ch = fmt[i]
            if ch == '%' and i + 1 < len(fmt):
                i += 1
                while fmt[i] in '0123456789-': i += 1
                v = args[k]; k += 1
                if fmt[i] == 'd': out += str(v - (1 << 32) if v & 0x80000000 else v)
                elif fmt[i] == 's': out += self.cstr(v)
                elif fmt[i] == 'x': out += '%x' % v
                else: out += '?'
            else:
                out += ch
            i += 1
        self.text[sid] = self.text.get(sid, '') + out
        self.ret(len(out))

    def h_FntFlush(self, *a):
        sid = self.arg(0)
        self.textout[sid] = self.text.get(sid, '')
        self.text[sid] = ''
        self.ret(0)

    def h_CdSearchFile(self, *a):
        dst = self.arg(0); name = self.cstr(self.arg(1)).lstrip('\\').split(';')[0]
        path = os.path.join(self.files_dir, name.lower())
        if not os.path.exists(path):
            print('CD: file not found', name); self.ret(0); return
        data = open(path, 'rb').read()
        self.cdfiles.append(data)
        self.wr(dst, struct.pack('<BBBBi16s', len(self.cdfiles) - 1, 0, 0, 1, len(data), name.encode()[:15]))
        self.ret(dst)

    def h_CdControl(self, *a):
        cmd = self.arg(0); p = self.arg(1)
        if cmd == 2:
            self.cd_cur = self.rd(p, 1)[0]
        if not hasattr(self, 'cd_log'): self.cd_log = []
        self.cd_log.append((self.frames, cmd, self.rd(p, 4) if p else b''))
        self.ret(1)

    def h_CdRead(self, *a):
        n = self.arg(0); data = self.cdfiles[self.cd_cur]
        self.wr(self.arg(1), data[:n * 2048])
        self.ret(1)

    def h_CdReadSync(self, *a): self.ret(0)

    # ---- running ----
    def start(self):
        uc = self.uc
        uc.reg_write(SP, 0x801FFF00)
        uc.reg_write(GP, self.sym.get('_gp', 0))
        uc.reg_write(RA, 0x80000100)
        self.pc = self.sym['main']

    def run_frames(self, n):
        self.stop_after = self.frames + n
        self.resume_skip = (self.pc == self.sym['DrawOTag'])
        self.uc.emu_start(self.pc, 0x80000100)
        self.pc = self.uc.reg_read(UC_MIPS_REG_PC)

    def screenshot(self, path=None, scale=2):
        from PIL import Image
        x, y, w, h = self.draw_env[:4]
        im = Image.new('RGB', (w, h))
        px = im.load()
        for j in range(h):
            row = (y + j) * 1024 + x
            for i in range(w):
                c = self.vram[row + i]
                px[i, j] = ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
        if scale != 1:
            im = im.resize((w * scale, h * scale), Image.NEAREST)
        if path:
            im.save(path)
        return im

    def top(self, n=14):
        tot = sum(self.prof.values()) or 1
        return [(k, v, 100.0 * v / tot) for k, v in sorted(self.prof.items(), key=lambda kv: -kv[1])[:n]]


if __name__ == '__main__':
    elf = sys.argv[1]
    r = Rig(elf, os.path.join(HERE, '..', 'maps'))
    r.start()
    r.run_frames(int(sys.argv[2]) if len(sys.argv) > 2 else 6)
    print('frames', r.frames, 'log', r.frame_log[-6:])
    print('text', r.textout)
    for k, v, p in r.top():
        print('%-28s %9d %5.1f%%' % (k, v, p))
    r.screenshot(sys.argv[3] if len(sys.argv) > 3 else '/tmp/rig.png')
