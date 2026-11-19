#!/usr/bin/env python3
"""mdlview.py - render MDL1 models to a contact sheet (software rasteriser, 4bpp skin sampling) to check them without hardware."""
import sys, struct, math
import numpy as np
from PIL import Image, ImageDraw

def parse(path):
    d = open(path, 'rb').read(); n = struct.unpack_from('<H', d, 4)[0]
    offs = struct.unpack_from('<%dI' % n, d, 8); names = open(path + '.names').read().split()
    out = {}
    for nm, o in zip(names, offs):
        nv, nf, t0, t1, ms, _ = struct.unpack_from('<6H', d, o); p = o + 12
        clips = [struct.unpack_from('<BBH', d, p + 4 * i) for i in range(7)]; p += 28
        tri0 = [struct.unpack_from('<9B', d, p + 9 * i) for i in range(t0)]; p += 9 * t0
        tri1 = [struct.unpack_from('<9B', d, p + 9 * i) for i in range(t1)]; p += 9 * t1
        fr = np.frombuffer(d, np.int8, nf * nv * 3, p).reshape(nf, nv, 3).astype(float) * ms / 256.0; p += nf * nv * 3; p = o + ((p - o + 3) & ~3)
        px = np.frombuffer(d, np.uint8, 8192, p); p += 8192
        clut = struct.unpack_from('<16H', d, p)
        pal = [((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3) for c in clut]
        tex = np.zeros((128, 128, 3), np.uint8)
        for y in range(128):
            for x in range(128):
                b = px[y * 64 + x // 2]; i = (b >> 4) if x & 1 else (b & 15); tex[y, x] = pal[i]
        out[nm] = dict(tri0=tri0, tri1=tri1, fr=fr, tex=tex, clips=clips)
    return out

def render(m, frame, yaw, lod, S=160, pitch=0.25, tint=1.0):
    img = np.full((S, S, 3), 40, np.uint8); zb = np.full((S, S), 1e9)
    V = m['fr'][frame]; ext = np.abs(m['fr']).max()
    c, s = math.cos(yaw), math.sin(yaw); cp, sp = math.cos(pitch), math.sin(pitch)
    X = V[:, 0] * c + V[:, 2] * s; Z = -V[:, 0] * s + V[:, 2] * c; Y = V[:, 1]
    Y2 = Y * cp - Z * sp; Z2 = Y * sp + Z * cp
    cy = -(m['fr'][:, :, 1].min() + m['fr'][:, :, 1].max()) / 2 * 0
    sc = S * 0.45 / (ext * 1.0)
    sx = S / 2 + X * sc; sy = S / 2 + (Y2 - (m['fr'][:, :, 1].min() + m['fr'][:, :, 1].max()) / 2) * sc
    for t in (m['tri1'] if lod else m['tri0']):
        a, b, cc = t[0], t[1], t[2]
        P = np.array([[sx[a], sy[a]], [sx[b], sy[b]], [sx[cc], sy[cc]]]); uv = np.array([[t[3], t[4]], [t[5], t[6]], [t[7], t[8]]], float)
        zz = np.array([Z2[a], Z2[b], Z2[cc]])
        area = (P[1, 0] - P[0, 0]) * (P[2, 1] - P[0, 1]) - (P[2, 0] - P[0, 0]) * (P[1, 1] - P[0, 1])
        if area >= 0: continue                                  # backface (nclip)
        x0, x1 = int(max(0, P[:, 0].min())), int(min(S - 1, P[:, 0].max() + 1)); y0, y1 = int(max(0, P[:, 1].min())), int(min(S - 1, P[:, 1].max() + 1))
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                w0 = ((P[1, 0] - x) * (P[2, 1] - y) - (P[2, 0] - x) * (P[1, 1] - y)) / area
                w1 = ((P[2, 0] - x) * (P[0, 1] - y) - (P[0, 0] - x) * (P[2, 1] - y)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0: continue
                z = w0 * zz[0] + w1 * zz[1] + w2 * zz[2]
                if z >= zb[y, x]: continue
                u = w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]; v = w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]
                zb[y, x] = z; img[y, x] = np.clip(m['tex'][min(127, int(v)), min(127, int(u))] * tint, 0, 255)
    return img

if __name__ == '__main__':
    pak, out = sys.argv[1], sys.argv[2]; sel = sys.argv[3:]
    M = parse(pak); names = [n for n in M if not sel or n in sel]
    cols = 4; S = 160
    sheet = Image.new('RGB', (cols * S, len(names) * S))
    for r, n in enumerate(names):
        m = M[n]
        views = [(0, 0.5, 0), (0, 2.6, 0), (len(m['fr']) // 2, 0.5, 0), (0, 0.5, 1)]
        for c, (f, yaw, lod) in enumerate(views):
            sheet.paste(Image.fromarray(render(m, f, yaw, lod, S)), (c * S, r * S))
        print(n, 'ok', flush=True)
    sheet.save(out)
