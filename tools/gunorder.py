#!/usr/bin/env python3
"""Draw order for the first-person guns.

The PS1 has no depth buffer, so a model is drawn back to front.  For the hud guns, sorting triangles by their average depth
fails: long barrel triangles reach from beside the eye to the muzzle and sort behind the stock.  The gun is fixed relative to
the eye, so the correct order can be worked out here, once per stored animation frame, with a real depth buffer: every
pixel where two triangles overlap is a vote for which one is in front, and a topological sort of those votes gives an order
in which the painter's algorithm reproduces the depth-buffered picture.

Used by mkmodel.py; `python3 gunorder.py models.pak gun_shotg out.png` renders a comparison (sorted / ordered / reference)."""
import numpy as np

GUN_H, CX, CY, NEAR = 120, 160, 92, 19
W, H = 320, 184


def project(P):
    z = np.maximum(P[:, 2], 1e-3)
    return np.stack([CX + P[:, 0] * GUN_H / z, CY + P[:, 1] * GUN_H / z], 1), P[:, 2]


def visible_tris(P, tris):
    """front-facing (view space test, as the runtime) and not wholly behind the near plane"""
    keep = []
    for i, (a, b, c) in enumerate(tris):
        pa, pb, pc = P[a], P[b], P[c]
        if pa[2] < NEAR and pb[2] < NEAR and pc[2] < NEAR: continue
        n = np.cross(pb - pa, pc - pa)
        if np.dot(n, pa) >= 0: continue
        keep.append(i)
    return keep


def raster(P, tris, ids, scale=2):
    """per pixel, every (depth, tri) covering it.  Clips at the near plane like the runtime (polygon in view space)."""
    w, h = W * scale, H * scale
    layers = {}
    for i in ids:
        poly = [P[v] for v in tris[i]]
        out = []
        for k in range(3):
            s, e = poly[k], poly[(k + 1) % 3]
            if s[2] >= NEAR: out.append(s)
            if (s[2] >= NEAR) != (e[2] >= NEAR):
                t = (s[2] - NEAR) / (s[2] - e[2]); out.append(s + (e - s) * t)
        if len(out) < 3: continue
        Q = np.array(out)
        sx = (CX + Q[:, 0] * GUN_H / Q[:, 2]) * scale; sy = (CY + Q[:, 1] * GUN_H / Q[:, 2]) * scale; iz = 1.0 / Q[:, 2]
        for k in range(1, len(Q) - 1):
            j = [0, k, k + 1]
            x0, x1 = int(max(0, np.floor(sx[j].min()))), int(min(w - 1, np.ceil(sx[j].max())))
            y0, y1 = int(max(0, np.floor(sy[j].min()))), int(min(h - 1, np.ceil(sy[j].max())))
            if x0 > x1 or y0 > y1: continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            (ax, bx, cx), (ay, by, cy) = sx[j], sy[j]
            den = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
            if abs(den) < 1e-9: continue
            l1 = ((xs - ax) * (cy - ay) - (cx - ax) * (ys - ay)) / den
            l2 = ((bx - ax) * (ys - ay) - (xs - ax) * (by - ay)) / den
            l0 = 1 - l1 - l2
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not inside.any(): continue
            z = l0 * iz[j[0]] + l1 * iz[j[1]] + l2 * iz[j[2]]       # 1/z: bigger is nearer
            for yy, xx in zip(*np.nonzero(inside)):
                layers.setdefault((y0 + yy, x0 + xx), []).append((z[yy, xx], i))
    return layers


def draw_order(P, tris):
    """triangle indices, far to near, for one frame (back faces and near-clipped-away triangles are left out)"""
    ids = visible_tris(P, tris)
    layers = raster(P, tris, ids)
    votes = {}
    for lst in layers.values():
        if len(lst) < 2: continue
        lst.sort()                                   # far ... near
        for a in range(len(lst)):
            for b in range(a + 1, len(lst)):
                fa, fb = lst[a][1], lst[b][1]
                if fa == fb: continue
                votes[(fa, fb)] = votes.get((fa, fb), 0) + 1        # fa is behind fb here
    # edges: a before b when a is behind b on more pixels than the reverse
    succ = {i: set() for i in ids}; indeg = {i: 0 for i in ids}; weight = {}
    for (a, b), n in votes.items():
        if n > votes.get((b, a), 0):
            succ[a].add(b); indeg[b] += 1; weight[(a, b)] = n - votes.get((b, a), 0)
    # Kahn's sort; inside a cycle drop the node whose incoming edges carry the fewest votes
    zavg = {i: float(np.mean([P[v][2] for v in tris[i]])) for i in ids}
    order, ready, done = [], [i for i in ids if indeg[i] == 0], set()
    while len(order) < len(ids):
        if not ready:
            rest = [i for i in ids if i not in done]
            inw = {i: 0 for i in rest}
            for (a, b), wgt in weight.items():
                if a not in done and b in inw: inw[b] += wgt
            pick = min(rest, key=lambda i: (inw[i], -zavg[i]))
            indeg[pick] = 0; ready.append(pick)
        ready.sort(key=lambda i: -zavg[i])            # among free choices, farthest first
        n = ready.pop(0)
        if n in done: continue
        done.add(n); order.append(n)
        for s in succ[n]:
            if s in done: continue
            indeg[s] -= 1
            if indeg[s] == 0: ready.append(s)
    return order


if __name__ == '__main__':
    import sys, struct
    from PIL import Image
    pak, name, out = sys.argv[1], sys.argv[2], sys.argv[3]
    names = open(pak + '.names').read().split()
    d = open(pak, 'rb').read(); idx = names.index(name)
    o = struct.unpack_from('<I', d, 8 + 4 * idx)[0]
    nv, nf, nt0, nt1, ms, fl = struct.unpack_from('<6H', d, o)
    tp = o + 40; tris = [tuple(d[tp + 9 * i:tp + 9 * i + 3]) for i in range(nt0)]
    fp = tp + 9 * (nt0 + nt1)
    f = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    P = np.array(struct.unpack_from('<%db' % (nv * 3), d, fp + f * nv * 3), float).reshape(nv, 3) * ms / 256.0
    ids = visible_tris(P, tris)
    col = {i: ((i * 97) % 200 + 55, (i * 57) % 200 + 55, (i * 31) % 200 + 55) for i in range(nt0)}
    def paint(seq):
        img = np.zeros((H * 2, W * 2, 3), np.uint8)
        for i in seq:
            for (y, x), lst in raster(P, tris, [i]).items(): img[y, x] = col[i]
        return img
    sortz = sorted(ids, key=lambda i: -np.mean([P[v][2] for v in tris[i]]))
    ref = np.zeros((H * 2, W * 2, 3), np.uint8)
    for (y, x), lst in raster(P, tris, ids).items(): ref[y, x] = col[max(lst)[1]]
    a, b = paint(sortz), paint(draw_order(P, tris))
    bad = lambda img: int((np.abs(img.astype(int) - ref.astype(int)).sum(2) > 0).sum())
    print('%s frame %d: pixels wrong vs depth buffer: average-depth sort %d, computed order %d' % (name, f, bad(a), bad(b)))
    Image.fromarray(np.concatenate([a, b, ref], 1)).save(out)
