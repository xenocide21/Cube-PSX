"""Sound chip (SPU) stand-in for the rig: sound RAM, the SDK's transfer calls, and the voice registers.  Every key-on is
logged with its sample address, pitch and volume (and later volume changes), so tests can see what was played and
render() can mix it to a WAV - decoding the SPU ADPCM in sound RAM, at the voice's pitch, with its volumes."""
import struct, wave
import numpy as np
from unicorn import UC_HOOK_MEM_WRITE

FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


class SPU:
    def __init__(self, rig):
        self.rig = rig
        self.ram = bytearray(512 * 1024)
        self.taddr = 0
        self.regs = {}
        self.events = []                 # (time in vblanks, kind, voice, a, b, c): kind 'on' (addr, pitch, (l, r)), 'vol' (l, r), 'off'
        uc = rig.uc
        uc.mem_map(0x1F801000, 0x1000)
        uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write, begin=0x1F801C00, end=0x1F801DFF)
        for n in ('SpuSetTransferMode', 'SpuSetTransferStartAddr', 'SpuWrite', 'SpuIsTransferCompleted'):
            if n in rig.sym:
                a = rig.sym[n]
                rig.wr(a, struct.pack('<II', 0x03E00008, 0))
                rig.cop2.pop(a, None); rig.cop2.pop(a + 4, None)
                uc.hook_add(__import__('unicorn').UC_HOOK_CODE, getattr(self, 'h_' + n), begin=a, end=a)

    def now(self):
        return self.rig.cycles() / self.rig.FRAME_CYC

    def h_SpuSetTransferMode(self, *a): self.rig.ret(0)
    def h_SpuIsTransferCompleted(self, *a): self.rig.ret(1)
    def h_SpuSetTransferStartAddr(self, *a): self.taddr = self.rig.arg(0); self.rig.ret(self.taddr)
    def h_SpuWrite(self, *a):
        src, size = self.rig.arg(0), self.rig.arg(1)
        self.ram[self.taddr:self.taddr + size] = self.rig.rd(src, size)
        self.rig.ret(size)

    def on_write(self, uc, access, addr, size, value, user):
        off = addr - 0x1F801C00
        self.regs[off] = value & 0xFFFF
        if off < 0x180 and off & 15 in (0, 2):                    # a voice's volume changed
            v = off >> 4
            self.events.append((self.now(), 'vol', v, self.regs.get(v * 16, 0), self.regs.get(v * 16 + 2, 0), 0))
        if off in (0x188, 0x18A, 0x18C, 0x18E):                   # key on / key off
            base = 0 if off in (0x188, 0x18C) else 16
            for b in range(16):
                if value & (1 << b):
                    v = base + b
                    if v >= 24: continue
                    if off in (0x188, 0x18A):
                        self.events.append((self.now(), 'on', v, self.regs.get(v * 16 + 6, 0) * 8, self.regs.get(v * 16 + 4, 0),
                                            (self.regs.get(v * 16, 0), self.regs.get(v * 16 + 2, 0))))
                    else:
                        self.events.append((self.now(), 'off', v, 0, 0, 0))

    def decode(self, addr):
        out = []; s1 = s2 = 0
        while addr + 16 <= len(self.ram):
            hdr, flags = self.ram[addr], self.ram[addr + 1]
            sh, f = hdr & 15, min(4, hdr >> 4); k1, k2 = FILTERS[f]
            for i in range(28):
                nib = (self.ram[addr + 2 + i // 2] >> ((i & 1) * 4)) & 15
                if nib >= 8: nib -= 16
                y = ((nib << 12) >> sh) + ((s1 * k1 + s2 * k2 + 32) >> 6)
                y = max(-32768, min(32767, y)); out.append(y); s2, s1 = s1, y
            addr += 16
            if flags & 1: break
        return np.array(out, dtype=np.float64)

    def keyons(self):
        return [e for e in self.events if e[1] == 'on']

    def render(self, path, t0, t1, voices=None):
        """mix everything keyed on between vblank t0 and t1 into a 44.1 kHz stereo WAV"""
        rate = 44100; n = int((t1 - t0) * rate / 60) + 1
        mix = np.zeros((n, 2))
        cache = {}
        ev = sorted(self.events)
        for i, (t, kind, v, addr, pitch, vol) in enumerate(ev):
            if kind != 'on' or t < t0 or t > t1 or (voices is not None and v not in voices): continue
            if addr not in cache: cache[addr] = self.decode(addr)
            smp = cache[addr]
            step = pitch / 4096.0
            length = int(len(smp) / step) if step > 0 else 0
            pos = np.arange(length) * step
            y = np.interp(pos, np.arange(len(smp)), smp)
            start = int((t - t0) * rate / 60)
            # volume over time: the key-on volume, then any changes until this voice is keyed again or off
            vols = [(t, vol[0], vol[1])]
            for (t2, k2, v2, a2, b2, c2) in ev[i + 1:]:
                if v2 != v or t2 < t: continue
                if k2 in ('on', 'off'): length = min(length, int((t2 - t) * rate / 60)); break
                if k2 == 'vol': vols.append((t2, a2, b2))
            y = y[:length]
            gl = np.zeros(length); gr = np.zeros(length)
            for j, (tv, l, r) in enumerate(vols):
                a = int((tv - t) * rate / 60); b = int((vols[j + 1][0] - t) * rate / 60) if j + 1 < len(vols) else length
                a = max(0, a); b = min(length, b)
                gl[a:b] = (l & 0x7FFF) / 0x4000; gr[a:b] = (r & 0x7FFF) / 0x4000
            end = min(n, start + length)
            if end > start:
                mix[start:end, 0] += y[:end - start] * gl[:end - start]
                mix[start:end, 1] += y[:end - start] * gr[:end - start]
        peak = np.max(np.abs(mix)) if n else 0
        self.clipped = float(np.mean(np.abs(mix) > 32767)) if n else 0.0
        self.last_mix = mix
        w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(np.clip(mix, -32768, 32767).astype(np.int16).tobytes()); w.close()
        return peak
