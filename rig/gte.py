"""PlayStation GTE (COP2) model for the test rig: registers, RTPS/RTPT, MVMVA, NCLIP, AVSZ3/4, SQR, OP.
Follows the psx-spx description, including the UNR divider and the saturation flags the renderer relies on."""

UNR = [max(0, (0x40000 // (i + 0x100) + 1) // 2 - 0x101) for i in range(0x101)]


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


class GTE:
    def __init__(self):
        self.d = [0] * 32   # data registers (stored as python ints, signed where the register is signed)
        self.c = [0] * 32   # control registers
        self.flag = 0
        self.ops = {}

    # ---- register access (values as 32-bit words) ----
    def read_data(self, r):
        d = self.d
        if r in (1, 3, 5, 8, 9, 10, 11):
            return s16(d[r]) & 0xFFFFFFFF
        if r in (7, 16, 17, 18, 19):
            return d[r] & 0xFFFF
        if r == 15:
            return d[14] & 0xFFFFFFFF
        if r in (28, 29):
            def c5(v):
                v = s16(v) >> 7
                return 0 if v < 0 else 31 if v > 31 else v
            return c5(d[9]) | (c5(d[10]) << 5) | (c5(d[11]) << 10)
        if r == 31:
            v = d[30] & 0xFFFFFFFF
            if v & 0x80000000:
                v = ~v & 0xFFFFFFFF
            return 32 if v == 0 else 32 - v.bit_length()
        return d[r] & 0xFFFFFFFF

    def write_data(self, r, v):
        d = self.d
        v &= 0xFFFFFFFF
        if r == 15:
            d[12] = d[13]; d[13] = d[14]; d[14] = v
        elif r == 28:
            d[9] = (v & 31) << 7; d[10] = ((v >> 5) & 31) << 7; d[11] = ((v >> 10) & 31) << 7
        elif r in (29, 31):
            pass
        else:
            d[r] = v

    def read_ctrl(self, r):
        if r == 31:
            f = self.flag & 0x7FFFF000
            if f & 0x7F87E000:
                f |= 0x80000000
            return f
        if r in (4, 12, 20, 26, 27, 29, 30):
            return s16(self.c[r]) & 0xFFFFFFFF
        return self.c[r] & 0xFFFFFFFF

    def write_ctrl(self, r, v):
        if r == 31:
            self.flag = v & 0x7FFFF000
        else:
            self.c[r] = v & 0xFFFFFFFF

    # ---- helpers ----
    def _mat(self, base):
        c = self.c
        return [[s16(c[base]), s16(c[base] >> 16), s16(c[base + 1])],
                [s16(c[base + 1] >> 16), s16(c[base + 2]), s16(c[base + 2] >> 16)],
                [s16(c[base + 3]), s16(c[base + 3] >> 16), s16(c[base + 4])]]

    def _vec(self, i):
        d = self.d
        return [s16(d[i * 2]), s16(d[i * 2] >> 16), s16(d[i * 2 + 1])]

    def _lim_ir(self, i, v, lm):
        lo = 0 if lm else -0x8000
        if v < lo:
            self.flag |= (1 << (25 - i)); return lo
        if v > 0x7FFF:
            self.flag |= (1 << (25 - i)); return 0x7FFF
        return v

    def _set_mac_ir(self, m, lm):
        for i in range(3):
            v = m[i]
            if v > 0x7FFFFFFF: self.flag |= 1 << (30 - i)
            if v < -0x80000000: self.flag |= 1 << (27 - i)
            self.d[25 + i] = s32(v)
            self.d[9 + i] = self._lim_ir(i + 1, s32(v), lm) & 0xFFFF

    def _divide(self, h, sz3):
        if h < sz3 * 2:
            z = 16 - sz3.bit_length() if sz3 else 16
            n = h << z
            dd = sz3 << z
            u = UNR[(dd - 0x7FC0) >> 7] + 0x101
            dd = (0x2000080 - dd * u) >> 8
            dd = (0x0000080 + dd * u) >> 8
            return min(0x1FFFF, (n * dd + 0x8000) >> 16)
        self.flag |= 1 << 17
        return 0x1FFFF

    def _rtp(self, v, sf, lm, last):
        c = self.c; d = self.d
        R = self._mat(0)
        tr = [s32(c[5]), s32(c[6]), s32(c[7])]
        mac = [((tr[i] << 12) + R[i][0] * v[0] + R[i][1] * v[1] + R[i][2] * v[2]) >> (sf * 12) for i in range(3)]
        for i in range(3):
            if mac[i] > 0x7FFFFFFF: self.flag |= 1 << (30 - i)
            if mac[i] < -0x80000000: self.flag |= 1 << (27 - i)
            d[25 + i] = s32(mac[i])
        d[9] = self._lim_ir(1, s32(mac[0]), lm) & 0xFFFF
        d[10] = self._lim_ir(2, s32(mac[1]), lm) & 0xFFFF
        # IR3: flag is judged on the >>12 value, the stored value on the saturated MAC3
        z = s32(mac[2]) if sf else s32(mac[2]) >> 12
        if z < -0x8000 or z > 0x7FFF: self.flag |= 1 << 22
        lo = 0 if lm else -0x8000
        d[11] = max(lo, min(0x7FFF, s32(mac[2]))) & 0xFFFF
        sz = z
        if sz < 0: sz = 0; self.flag |= 1 << 18
        if sz > 0xFFFF: sz = 0xFFFF; self.flag |= 1 << 18
        d[16] = d[17]; d[17] = d[18]; d[18] = d[19]; d[19] = sz
        n = self._divide(c[26] & 0xFFFF, sz)
        ofx = s32(c[24]); ofy = s32(c[25])
        sx = (n * s16(d[9]) + ofx) >> 16
        sy = (n * s16(d[10]) + ofy) >> 16
        if sx < -0x400: sx = -0x400; self.flag |= 1 << 14
        if sx > 0x3FF: sx = 0x3FF; self.flag |= 1 << 14
        if sy < -0x400: sy = -0x400; self.flag |= 1 << 13
        if sy > 0x3FF: sy = 0x3FF; self.flag |= 1 << 13
        d[12] = d[13]; d[13] = d[14]; d[14] = (sx & 0xFFFF) | ((sy & 0xFFFF) << 16)
        if last:
            m0 = n * s16(c[27]) + s32(c[28])
            d[24] = s32(m0)
            ir0 = m0 >> 12
            if ir0 < 0: ir0 = 0; self.flag |= 1 << 12
            if ir0 > 0x1000: ir0 = 0x1000; self.flag |= 1 << 12
            d[8] = ir0

    # ---- command execution ----
    def command(self, w):
        op = w & 0x3F
        sf = (w >> 19) & 1
        lm = (w >> 10) & 1
        self.flag = 0
        self.ops[op] = self.ops.get(op, 0) + 1
        d = self.d; c = self.c
        if op == 0x01:
            self._rtp(self._vec(0), sf, lm, True)
        elif op == 0x30:
            self._rtp(self._vec(0), sf, lm, False)
            self._rtp(self._vec(1), sf, lm, False)
            self._rtp(self._vec(2), sf, lm, True)
        elif op == 0x06:
            x0, y0 = s16(d[12]), s16(d[12] >> 16)
            x1, y1 = s16(d[13]), s16(d[13] >> 16)
            x2, y2 = s16(d[14]), s16(d[14] >> 16)
            d[24] = s32(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1)
        elif op in (0x2D, 0x2E):
            if op == 0x2D:
                m = s16(c[29]) * (d[17] + d[18] + d[19])
            else:
                m = s16(c[30]) * (d[16] + d[17] + d[18] + d[19])
            d[24] = s32(m)
            o = m >> 12
            if o < 0: o = 0; self.flag |= 1 << 18
            if o > 0xFFFF: o = 0xFFFF; self.flag |= 1 << 18
            d[7] = o
        elif op == 0x12:
            mx = (w >> 17) & 3; vv = (w >> 15) & 3; cv = (w >> 13) & 3
            M = self._mat([0, 8, 16][mx]) if mx < 3 else [[0] * 3] * 3
            V = self._vec(vv) if vv < 3 else [s16(d[9]), s16(d[10]), s16(d[11])]
            T = [0, 0, 0] if cv == 3 else [s32(c[b]) for b in ([5, 6, 7], [13, 14, 15], [21, 22, 23])[cv]]
            self._set_mac_ir([((T[i] << 12) + M[i][0] * V[0] + M[i][1] * V[1] + M[i][2] * V[2]) >> (sf * 12) for i in range(3)], lm)
        elif op == 0x28:
            self._set_mac_ir([(s16(d[9 + i]) * s16(d[9 + i])) >> (sf * 12) for i in range(3)], lm)
        elif op == 0x0C:
            R = self._mat(0); a = [R[0][0], R[1][1], R[2][2]]; ir = [s16(d[9]), s16(d[10]), s16(d[11])]
            self._set_mac_ir([(ir[2] * a[1] - ir[1] * a[2]) >> (sf * 12), (ir[0] * a[2] - ir[2] * a[0]) >> (sf * 12), (ir[1] * a[0] - ir[0] * a[1]) >> (sf * 12)], lm)
        else:
            raise RuntimeError('GTE command %02x not modelled' % op)
