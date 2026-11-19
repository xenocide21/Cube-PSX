#!/usr/bin/env python3
"""mkspace.py - the space sky box for Facing Cubes: stars, a planet filling the sky below (oceans, land, clouds, a blue rim of
atmosphere, lit from the sun), and the sun itself far off.  Ray traced per face in the renderer's own face layout
(render.c sky_faces: ft -Z, bk +Z, lf +X, rt -X, dn +Y, up -Y in PS axes, y down), then packed like mksky.py ('SKY1').

usage: mkspace.py out.pak [preview.png]
"""
import sys, struct, math
import numpy as np
from PIL import Image

R = 128                                          # rendered at the size it is packed at: stars stay single bright pixels
FACES = [  # corners top-left, top-right, bottom-left (x, y, z) as in render.c
    ((1, -1, -1), (-1, -1, -1), (1, 1, -1)),     # ft
    ((-1, -1, 1), (1, -1, 1), (-1, 1, 1)),       # bk
    ((1, -1, 1), (1, -1, -1), (1, 1, 1)),        # lf
    ((-1, -1, -1), (-1, -1, 1), (-1, 1, -1)),    # rt
    ((-1, 1, 1), (1, 1, 1), (-1, 1, -1)),        # dn
    ((-1, -1, -1), (1, -1, -1), (-1, -1, 1)),    # up
]

def norm(v): return v / np.linalg.norm(v, axis=-1, keepdims=True)
SUN = norm(np.array([0.55, -0.30, 0.78]))        # high-ish, off to one side
PC = norm(np.array([-0.35, 1.0, 0.25]))          # where the planet's middle is (below, a little aside)
PR = math.radians(58)                            # its angular radius

rng = np.random.RandomState(7)
# stars: random directions, a few bright ones
NS = 4000
sd = norm(rng.normal(size=(NS, 3))); sb = rng.uniform(0.25, 1.0, NS) ** 3

def fbm(p, oct=5):
    """cheap value-noise fbm on a unit-sphere point (N,3) -> (N,)"""
    out = np.zeros(len(p)); amp = 0.5; f = 2.0
    for o in range(oct):
        q = p * f + o * 17.3
        out += amp * (np.sin(q[:, 0] * 1.7 + np.sin(q[:, 1] * 2.3)) * np.cos(q[:, 1] * 1.3 + np.sin(q[:, 2] * 1.9)) * np.sin(q[:, 2] * 1.1 + q[:, 0] * 0.7))
        amp *= 0.5; f *= 2.03
    return out

def shade(d):
    """d: (N,3) unit view directions -> (N,3) rgb 0..1"""
    n = len(d); col = np.zeros((n, 3))
    # stars
    cosang = d @ sd.T
    hit = cosang > math.cos(math.radians(0.42))
    col += (hit * sb).max(axis=1)[:, None] * np.array([1.0, 0.97, 0.9])
    # the sun: disc and glow
    cs = d @ SUN; ang = np.arccos(np.clip(cs, -1, 1))
    col += np.exp(-(ang / math.radians(9)) ** 2)[:, None] * np.array([1.0, 0.85, 0.55]) * 0.6
    col[ang < math.radians(2.6)] = [1.0, 0.98, 0.9]
    # the planet
    cp = d @ PC; a = np.arccos(np.clip(cp, -1, 1))
    on = a < PR
    if on.any():
        dd = d[on]; r = math.sin(PR)                       # sphere: centre PC (distance 1), radius r
        b = dd @ PC; t = b - np.sqrt(np.clip(b * b - (1 - r * r), 0, None))
        nrm = (t[:, None] * dd - PC[None, :]) / r          # surface normal
        land = fbm(nrm * 1.6) + 0.15 * fbm(nrm * 6.0, 3)
        cloud = fbm(nrm * 3.1 + 5.0, 4) + 0.25 * fbm(nrm * 9.0 + 2.0, 3)
        base = np.where(land[:, None] > 0.06, np.array([0.50, 0.42, 0.26]), np.array([0.04, 0.16, 0.45]))
        base = np.where((land[:, None] > 0.06) & (land[:, None] < 0.13), np.array([0.24, 0.40, 0.17]), base)
        cl = np.clip((cloud - 0.04) * 3.0, 0, 1)[:, None]
        surf = base * (1 - cl) + np.array([0.95, 0.96, 1.0]) * cl
        lam = np.clip(nrm @ SUN, 0, 1)
        surf = surf * (0.10 + 0.95 * lam)[:, None]
        mu = np.clip(-(dd * nrm).sum(1), 0, 1)              # 1 face on .. 0 at the limb
        rim = (1 - mu) ** 3
        surf = surf * (1 - 0.5 * rim[:, None]) + np.array([0.35, 0.6, 1.0]) * rim[:, None] * (0.25 + 0.75 * lam[:, None])
        col[on] = surf
    # the glow just outside the limb
    halo = np.clip(1 - (a - PR) / math.radians(5), 0, 1) * (a >= PR)
    col += (halo ** 2)[:, None] * np.array([0.25, 0.45, 1.0]) * 0.8
    return np.clip(col, 0, 1)

def face(i):
    c0, c1, c2 = (np.array(c, float) for c in FACES[i])
    u = (np.arange(R) + 0.5) / R
    out = np.zeros((R, R, 3))
    for row in range(R):
        if i >= 4:                           # dn / up: texture pixel (u, v) shows grid (col = v, row = R-1-u) (render.c)
            # build per texture pixel: v = this row index, u = column index
            v = (row + 0.5) / R; cols = 1 - u
            dirs = c0[None, :] + (c1 - c0)[None, :] * v + (c2 - c0)[None, :] * cols[:, None]
        else:
            dirs = c0[None, :] + (c1 - c0)[None, :] * u[:, None] + (c2 - c0)[None, :] * ((row + 0.5) / R)
        out[row] = shade(norm(dirs))
    return out

def main():
    out = sys.argv[1]; buf = b'SKY1'; ims = []
    for i in range(6):
        a = face(i); im = Image.fromarray((a * 255).astype(np.uint8)); ims.append(im)
        # palette: black, three star greys and the sun's white always; the other 12 fitted to what else is on the face
        # (a plain median cut spends all 16 on the black and the planet and averages the stars away)
        fixed = [(0, 0, 0), (255, 255, 250), (180, 178, 170), (105, 104, 100)]
        px_ = np.asarray(im).reshape(-1, 3); rest = px_[(px_.max(1) > 12) & ~((np.abs(px_[:, 0].astype(int) - px_[:, 2]) < 12) & (px_.max(1) > 90))]
        extra = []
        if len(rest) > 64:
            sm = Image.fromarray(rest[None, :, :].astype(np.uint8)).quantize(colors=12, method=Image.Quantize.MEDIANCUT)
            pe = sm.getpalette()[:36]; extra = [tuple(pe[k * 3:k * 3 + 3]) for k in range(12)]
        palc = (fixed + extra + [(0, 0, 0)] * 16)[:16]
        pim = Image.new('P', (1, 1)); pim.putpalette([c for t in palc for c in t] + [0] * (768 - 48))
        q = im.quantize(palette=pim, dither=Image.Dither.NONE)
        pal = (q.getpalette() or [])[:48]; pal += [0] * (48 - len(pal)); px = q.tobytes(); packed = bytearray()
        for k in range(0, len(px), 2): packed.append((px[k] & 15) | ((px[k + 1] & 15) << 4))
        def c555(r, g, b):
            v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10); return v or 0x0421
        buf += bytes(packed) + b''.join(struct.pack('<H', c555(*pal[k * 3:k * 3 + 3])) for k in range(16))
    open(out, 'wb').write(buf); print(out, len(buf))
    if len(sys.argv) > 2:                    # a cross preview: up / lf ft rt bk / dn
        sh = Image.new('RGB', (128 * 4, 128 * 3))
        for i, (x, y) in enumerate([(1, 1), (3, 1), (0, 1), (2, 1), (1, 2), (1, 0)]): sh.paste(ims[i], (x * 128, y * 128))
        sh.save(sys.argv[2])

main()
