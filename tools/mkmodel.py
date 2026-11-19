#!/usr/bin/env python3
"""mkmodel.py - convert Cube's MD2 models into the PS1 "MDL1" pack used by Cube PSX.

For each model: weld duplicate vertices, decimate with quadric edge collapse (error summed over several animation frames, so
moving parts keep their detail), keep per-corner UVs, keep a hand-picked subset of animation frames, quantise vertices to int8
and the skin to 128x128 / 16 colours.  Each model gets two levels of detail sharing one vertex pool.

Pack layout (little endian):
  'MDL1', u16 count, u16 0, u32 offset[count]
  model: u16 nverts, nframes, ntri0, ntri1, mscale(ps units per q step *256), u16 flags (bit 0: draw orders follow)
         8 x clip {u8 first, u8 count, u16 range}   (range = original md2 frame count, 100 ms each)
         (flags bit 1: map prop - its skin is in each map's texture pack, slot 240 + prop number)
         ntri0 x {u8 a,b,c, u8 u0,v0,u1,v1,u2,v2}, then ntri1 x same (LOD 1)
         nframes x nverts x int8[3]    (x,y,z in the PS model space: x right, y down, z forward), padded to 4
         flags bit 0 (hud guns): nframes x ntri0 x u16 triangle draw order, far to near, padded to 4
         8192 bytes 4bpp skin, 32 bytes CLUT
Clip order: IDLE WALK ATTACK PAIN DEATH0 DEATH1 DEATH2 (unused = count 0)
"""
import sys, os, struct, heapq, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import md2
import gunorder
from PIL import Image

PK = '/home/claude/cube_tgz/cube/packages/models/'

# name, path, cube scale arg, grounded, tris LOD0, tris LOD1, clips[(first, range, count)] x7
STD = [(0, 40, 4), (40, 6, 6), (46, 8, 4), (54, 4, 2), (178, 6, 3), (184, 6, 3), (190, 8, 4)]
PIG = [(0, 1, 1), (1, 15, 6), (51, 19, 5), (32, 18, 4), (137, 28, 6), (137, 28, 6), (137, 28, 6)]
ONE = [(0, 1, 1)] + [(0, 0, 0)] * 6
GUN = [(6, 1, 1), (7, 18, 9)] + [(0, 0, 0)] * 5      # Cube: drawhudmodel(6,1) idle, drawhudmodel(7,18) fire

def monster(n, ms, hi, lo, clips=STD, k=1.0):
    return dict(name=n, path=PK + 'monster/%s/' % n, scale=ms / 10.0 * k, ground=True, hi=hi, lo=lo, clips=clips)
def item(n, hi):
    return dict(name=n, path=PK + n + '/', scale=1.0, ground=False, hi=hi, lo=hi, clips=ONE)
PISTOL_CLIPS = [(0, 1, 1), (1, 4, 9)] + [(0, 0, 0)] * 5     # idle; fire: 9 stored frames over 4 x 100 ms
def pistol_spec():
    """our pistol (its own slot, between the fist and the shotgun): a slide, a frame and an angled grip, in the space Cube's first-person guns use
    (x right, y down, z forward, from the eye); a recoil in 9 frames; skin assets/pistolskin.png"""
    return dict(name='gun_pistol', gen='pistol', path=PK, skin=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'pistolskin.png'),
                scale=1.0, ground=False, hi=200, lo=200, clips=PISTOL_CLIPS, hud=True, lift=0.6)

def gun(n, p, hi):
    return dict(name='gun_' + n, path=PK + 'hudguns/' + p + '/', scale=1.0, ground=False, hi=hi, lo=hi, clips=GUN, lift=0.5)

# map props (Cube's default mapmodel slots 0-8, data/default_map_settings.cfg): static, drawn at their origin; their
# skins travel in the texture pack of each map that uses them (mktex.py, slots 240+), so they take no global video memory
MAPMODELS = ['tree1', 'bridge', 'dcp/tree2', 'dcp/tree3', 'dcp/palmtree', 'dcp/thorns', 'dcp/plant1', 'dcp/grass', 'dcp/ivy', 'flag', 'clips']
MM_TRIS = [70, 24, 120, 90, 120, 90, 80, 50, 110, 40, 12]
FLAGSKIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'flagskin.png')
CLIPSKIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'clipskin.png')
def mapskin(k):
    """skin file of map prop k (the flag's is ours, assets/flagskin.png)"""
    return FLAGSKIN if MAPMODELS[k] == 'flag' else CLIPSKIN if MAPMODELS[k] == 'clips' else PK + MAPMODELS[k] + '/skin.jpg'
def mapmodel(i):
    n = MAPMODELS[i]
    if n == 'flag':        # CTF flag (ours): a pole and a waving cloth, 7 units tall; tinted red / blue when drawn
        return dict(name='mm_flag', gen='flag', path=PK, skin=FLAGSKIN, scale=1.0, ground=False, hi=40, lo=40, clips=ONE, mapskin=True)
    if n == 'clips':       # pistol ammo box (ours): a small olive crate, drawn as a pickup item
        return dict(name='mm_clips', gen='box', path=PK, skin=CLIPSKIN, scale=1.0, ground=False, hi=12, lo=12, clips=ONE, mapskin=True)
    return dict(name='mm_' + n.split('/')[-1], path=PK + n + '/', scale=1.0, ground=False, hi=MM_TRIS[i], lo=max(12, MM_TRIS[i] // 2), clips=ONE, mapskin=True)

def sphere_spec(name='explosion', swap=None):
    return dict(name=name, swap=swap, gen='sphere', path=PK, skin='/home/claude/cube_tgz/cube/data/explosion.jpg', scale=1.0, ground=False, hi=80, lo=80, clips=ONE)

MODELS = [
    monster('ogro', 10, 110, 50), monster('rhino', 8, 110, 50), monster('rat', 14, 110, 50), monster('slith', 18, 110, 50),
    monster('bauul', 24, 110, 50), monster('hellpig', 12, 90, 40, PIG, 32.0), monster('knight', 18, 110, 50), monster('goblin', 13, 110, 50),
    item('shells', 36), item('bullets', 70), item('rockets', 90), item('rrounds', 40), item('health', 60), item('boost', 70),
    item('g_armour', 60), item('y_armour', 60), item('quad', 80),
    gun('fist', 'fist', 9999), gun('shotg', 'shotg', 9999), gun('chaing', 'chaing', 9999), gun('rocket', 'rocket', 9999), gun('rifle', 'rifle', 9999),
    sphere_spec(), sphere_spec('fx_ice', (0, 2)), sphere_spec('fx_slime', (0, 1)),
    item('carrot', 84), item('teleporter', 120),          # trigger carrot and teleporter (entities.cpp renderentities)
] + [mapmodel(i) for i in range(len(MAPMODELS))] + [pistol_spec()]

def sample(first, rng, count):
    if count <= 0: return []
    if count == 1: return [first]
    return [first + int(round(i * (rng - 1) / (count - 1))) for i in range(count)]

def decimate(P, tris, targets):
    """P: (F,V,3) positions per sample frame; tris: list of (a,b,c) corner xyz indices (+ payload kept alongside).
    Returns {target: [tri index list kept with remapped vertices]}"""
    F, V, _ = P.shape
    # per-frame vertex quadrics
    Q = np.zeros((F, V, 4, 4))
    def faceplanes(tl):
        for ti, (a, b, c) in enumerate(tl):
            for f in range(F):
                n = np.cross(P[f, b] - P[f, a], P[f, c] - P[f, a]); ar = np.linalg.norm(n)
                if ar < 1e-9: continue
                n = n / ar; d = -np.dot(n, P[f, a]); pl = np.append(n, d)
                K = np.outer(pl, pl) * ar
                for v in (a, b, c): Q[f, v] += K
    faceplanes([t for t in tris])
    alive = [True] * V
    cur = [list(t) for t in tris]                   # current vertex ids of each tri
    tri_alive = [True] * len(tris)
    vt = [set() for _ in range(V)]
    for i, t in enumerate(cur):
        for v in t: vt[v].add(i)
    ver = [0] * V

    def nbrs(v):
        s = set()
        for i in vt[v]:
            for u in cur[i]:
                if u != v: s.add(u)
        return s

    def cost(a, b):                                   # collapse a -> b
        Qs = Q[:, a] + Q[:, b]
        h = np.concatenate([P[:, b], np.ones((F, 1))], axis=1)
        c = float(np.einsum('fi,fij,fj->', h, Qs, h))
        c += 0.02 * float(((P[:, a] - P[:, b]) ** 2).sum())
        # reject folds
        for i in vt[a]:
            t = cur[i]
            if b in t: continue
            for f in (0, F // 2):
                o = [P[f, x] for x in t]; n0 = np.cross(o[1] - o[0], o[2] - o[0])
                t2 = [b if x == a else x for x in t]; o2 = [P[f, x] for x in t2]; n1 = np.cross(o2[1] - o2[0], o2[2] - o2[0])
                l0, l1 = np.linalg.norm(n0), np.linalg.norm(n1)
                if l0 > 1e-9 and (l1 < 1e-9 or np.dot(n0, n1) / (l0 * l1) < 0.1): return None
        return c

    heap = []
    def push(a, b):
        c = cost(a, b)
        if c is not None: heapq.heappush(heap, (c, a, b, ver[a], ver[b]))
    for v in range(V):
        for u in nbrs(v): push(v, u)

    out = {}
    count = lambda: sum(1 for i, ok in enumerate(tri_alive) if ok)
    ntri = count()
    for tgt in sorted(targets, reverse=True):
        while ntri > tgt and heap:
            c, a, b, va, vb = heapq.heappop(heap)
            if not alive[a] or not alive[b] or ver[a] != va or ver[b] != vb: continue
            Q[:, b] += Q[:, a]; alive[a] = False; ver[b] += 1
            for i in list(vt[a]):
                t = cur[i]
                if b in t:
                    tri_alive[i] = False; ntri -= 1
                    for v in t: vt[v].discard(i)
                else:
                    cur[i] = [b if x == a else x for x in t]; vt[b].add(i)
            vt[a] = set()
            for u in nbrs(b):
                ver[u] += 0
                push(u, b); push(b, u)
        out[tgt] = [(i, tuple(cur[i])) for i in range(len(cur)) if tri_alive[i]]
    return out

def build_sphere(spec):
    phi = (1 + 5 ** 0.5) / 2
    V = [(-1, phi, 0), (1, phi, 0), (-1, -phi, 0), (1, -phi, 0), (0, -1, phi), (0, 1, phi), (0, -1, -phi), (0, 1, -phi), (phi, 0, -1), (phi, 0, 1), (-phi, 0, -1), (-phi, 0, 1)]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    V = [np.array(v, float) / np.linalg.norm(v) for v in V]; cache = {}
    def mid(a, b):
        k = (min(a, b), max(a, b))
        if k not in cache:
            m = (V[a] + V[b]) / 2; V.append(m / np.linalg.norm(m)); cache[k] = len(V) - 1
        return cache[k]
    F2 = []
    for a, b, c in F:
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        F2 += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
    P = np.array(V) * 64.0
    st = [(int((p[0] / 64 + 1) / 2 * 127), int((p[1] / 64 + 1) / 2 * 127)) for p in P]
    raw = [((a, c, b), (a, c, b)) for a, b, c in F2]
    m = dict(sw=128, sh=128, st=st, tris=[], frames=None, nxyz=len(V))
    return m, None, [0], P[None, :, :], raw, {80: [(i, r[0]) for i, r in enumerate(raw)]}, [(0, 1, 1)] + [(0, 0, 0)] * 6, [0]

def build_flag(spec):
    U = 64.0; V = []; st = []; raw = []
    def v(x, y, z, u, t): V.append((x * U, y * U, z * U)); st.append((int(u), int(t))); return len(V) - 1
    # pole: a thin square post, 7 units
    w = 0.15
    for (x0, z0, x1, z1) in ((-w, -w, w, -w), (w, -w, w, w), (w, w, -w, w), (-w, w, -w, -w)):
        a = v(x0, 0, z0, 2, 127); b = v(x1, 0, z1, 20, 127); c = v(x1, -7, z1, 20, 0); d = v(x0, -7, z0, 2, 0)
        raw += [((a, b, c), (a, b, c)), ((a, c, d), (a, c, d))]
    # cloth: 3 units across, 2.4 tall, from the top of the pole, in four waving strips, both sides
    cols = 5; top, bot = -6.9, -4.5
    tp = [v(i * 0.75, top, 0.25 * math.sin(i * 1.4), 32 + i * 23, 8) for i in range(cols)]
    bt = [v(i * 0.75, bot, 0.25 * math.sin(i * 1.4 + 0.5), 32 + i * 23, 119) for i in range(cols)]
    for i in range(cols - 1):
        a, b, c, d = tp[i], tp[i + 1], bt[i + 1], bt[i]
        raw += [((a, b, c), (a, b, c)), ((a, c, d), (a, c, d)), ((a, c, b), (a, c, b)), ((a, d, c), (a, d, c))]
    P = np.array(V)
    m = dict(sw=128, sh=128, st=st, tris=[], frames=None, nxyz=len(V))
    return m, None, [0], P[None, :, :], raw, {40: [(i, r[0]) for i, r in enumerate(raw)]}, [(0, 1, 1)] + [(0, 0, 0)] * 6, [0]

def build_box(spec):
    # a crate 1.5 x 1.0 x 1.0 units, centred on its origin like Cube's item models (they spin about it);
    # skin: top half of the texture the lid, bottom half the sides
    U = 64.0; hx, hy, hz = 0.75, 0.5, 0.5
    V = []; st = []; raw = []
    C = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz), (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
    for c in C: V.append(tuple(x * U for x in c))
    for f, top in (((0, 1, 2, 3), 0), ((5, 4, 7, 6), 0), ((4, 0, 3, 7), 0), ((1, 5, 6, 2), 0), ((4, 5, 1, 0), 1), ((3, 2, 6, 7), 1)):
        a, b, c, d = f
        u0, v0, u1, v1 = (0, 0, 127, 63) if top else (0, 64, 127, 127)
        ta = len(st); st += [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
        raw += [((a, b, c), (ta, ta + 1, ta + 2)), ((a, c, d), (ta, ta + 2, ta + 3))]
    P = np.array(V, float)
    m = dict(sw=128, sh=128, st=st, tris=[], frames=None, nxyz=len(V))
    return m, None, [0], P[None, :, :], raw, {12: [(i, r[0]) for i, r in enumerate(raw)]}, [(0, 1, 1)] + [(0, 0, 0)] * 6, [0]

def build_pistol(spec):
    # boxes: (x0, x1, y0, y1, z0, z1, uv region (u0, v0, u1, v1), slide?)  - y down, z forward
    boxes = [(10, 24, 13, 23, 22, 96, (0, 0, 127, 63), 1),       # slide
             (11, 23, 23, 28, 30, 84, (64, 64, 127, 127), 0),    # frame under it
             (11, 23, 28, 50, 26, 44, (0, 64, 63, 127), 0)]      # grip (sheared back below)
    V0 = []; st = []; raw = []; slide = []
    for (x0, x1, y0, y1, z0, z1, (u0, v0, u1, v1), sl) in boxes:
        base = len(V0)
        for (x, y, z) in [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]:
            if y1 == 50 and y == y1: z -= 9                     # the grip leans back
            V0.append((x, y, z)); slide.append(sl)
        for f in ((0, 1, 2, 3), (5, 4, 7, 6), (4, 0, 3, 7), (1, 5, 6, 2), (4, 5, 1, 0), (3, 2, 6, 7)):
            a, b, c, d = (base + i for i in f)
            ta = len(st); st += [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            raw += [((a, b, c), (ta, ta + 1, ta + 2)), ((a, c, d), (ta, ta + 2, ta + 3))]
    V0 = np.array(V0, float); frames = []
    kick = [0, 1.0, 0.85, 0.65, 0.45, 0.3, 0.18, 0.08, 0.03, 0]   # idle, then the recoil dying away
    for k in kick:
        P = V0.copy()
        P[:, 1] -= k * 6 * (P[:, 2] / 90.0)                    # the muzzle flips up
        P[:, 2] -= k * 4                                        # the whole gun comes back
        P[np.array(slide, bool), 2] -= k * 7                    # and the slide further
        frames.append(P)
    A = np.array(frames)
    m = dict(sw=128, sh=128, st=st, tris=[], frames=None, nxyz=len(V0))
    return m, None, list(range(len(kick))), A, raw, {200: [(i, r[0]) for i, r in enumerate(raw)]}, [(0, 1, 1), (1, 9, 4)] + [(0, 0, 0)] * 5, list(range(len(kick)))

def build(spec):
    if spec.get('gen') == 'sphere': return build_sphere(spec)
    if spec.get('gen') == 'pistol': return build_pistol(spec)
    if spec.get('gen') == 'flag': return build_flag(spec)
    if spec.get('gen') == 'box': return build_box(spec)
    m = md2.load(spec['path'] + 'tris.md2')
    fr = m['frames']
    k = spec['scale'] / 16.0
    # model space: x = -md2y, y = -md2z, z = md2x  (PS: x right, y down, z forward), cube units -> PS units (*64)
    def conv(v): return np.array([-v[1], -v[2], v[0]]) * k * 64.0
    # frames needed
    want = []
    clipinfo = []
    for first, rng, cnt in spec['clips']:
        idx = sample(first, rng, cnt)
        idx = [min(i, len(fr) - 1) for i in idx]
        clipinfo.append((len(want), len(idx), rng if cnt else 0))
        want += idx
    uniq = sorted(set(want))
    A = np.array([[conv(v) for v in fr[i][1]] for i in uniq])        # (F, V, 3)
    # weld vertices that coincide in all frames
    key = {}; remap = []
    for v in range(A.shape[1]):
        kk = tuple(np.round(A[:, v, :], 3).ravel())
        if kk not in key: key[kk] = v
        remap.append(key[kk])
    raw = []
    for t in m['tris']:
        a, b, c = [remap[x] for x in t[0:3]]
        if len({a, b, c}) < 3: continue
        raw.append(((a, b, c), tuple(t[3:6])))
    if spec['ground']:
        A[:, :, 1] -= A[0, :, 1].max()       # feet on y = 0 (y is down)
    # decimation sample frames
    sf = sorted(set(np.linspace(0, len(uniq) - 1, min(8, len(uniq))).round().astype(int)))
    P = A[sf]
    tl = [r[0] for r in raw]
    tg = sorted({spec['hi'], spec['lo']})
    if len(raw) <= min(tg):
        res = {t: [(i, raw[i][0]) for i in range(len(raw))] for t in tg}
    else:
        res = decimate(P, tl, tg)
    return m, fr, uniq, A, raw, res, clipinfo, want

def skin(path, swap=None, lift=None, stp=False):
    im = Image.open(path).convert('RGB').resize((128, 128), Image.LANCZOS)
    if lift:                                  # first-person guns are mostly near-black metal: lift the shadows so detail survives 16 colours and fog-free lighting
        a = np.asarray(im, np.float32) / 255.0
        im = Image.fromarray(np.clip(255.0 * np.power(a, lift) * 0.92 + 10, 0, 255).astype(np.uint8))
    if swap:
        ch = list(im.split()); ch[swap[0]], ch[swap[1]] = ch[swap[1]], ch[swap[0]]; im = Image.merge('RGB', ch)
    q = im.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = (q.getpalette() or [])[:48]; pal += [0] * (48 - len(pal))
    px = q.tobytes(); packed = bytearray()
    for i in range(0, len(px), 2): packed.append((px[i] & 15) | ((px[i + 1] & 15) << 4))
    def c555(r, g, b):
        v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10); return v or 0x0421
    # glowing effects are drawn additively: on the PlayStation a textured pixel is only blended when its colour has bit 15 set
    clut = b''.join(struct.pack('<H', c555(*pal[i * 3:i * 3 + 3]) | (0x8000 if stp else 0)) for i in range(16))
    return bytes(packed), clut, q

def emit(spec):
    m, fr, uniq, A, raw, res, clipinfo, want = build(spec)
    hi, lo = spec['hi'], spec['lo']
    thi = res[hi] if hi in res else res[max(res)]
    tlo = res[lo] if lo in res else res[min(res)]
    used = sorted({v for _, t in thi for v in t} | {v for _, t in tlo for v in t})
    ren = {v: i for i, v in enumerate(used)}
    allp = A[:, used, :]
    mx = float(np.abs(allp).max()); mscale = max(mx / 127.0, 0.01)
    Qn = np.clip(np.round(allp / mscale), -127, 127).astype(np.int8)
    st = m['st']; W, H = m['sw'], m['sh']
    def rec(i, t):
        o = raw[i]; b = bytearray([ren[t[0]], ren[t[1]], ren[t[2]]])
        for s in o[1]:
            u, v = st[s]; b += bytes([max(0, min(127, int(round(u / W * 127)))), max(0, min(127, int(round(v / H * 127))))])
        return bytes(b)
    has_order = spec['clips'] is GUN or spec.get('hud')
    blob = struct.pack('<6H', len(used), len(uniq), len(thi), len(tlo), int(round(mscale * 256)), (1 if has_order else 0) | (2 if spec.get('mapskin') else 0))
    fmap = {f: i for i, f in enumerate(uniq)}
    # clips refer to *stored* frame indices: rebuild by position in want
    for (first, cnt, rng) in clipinfo:
        sl = want[first:first + cnt]
        fi = fmap[sl[0]] if sl else 0
        # clips need consecutive stored frames; since uniq is sorted and clips are ascending runs this holds unless clips overlap
        blob += struct.pack('<BBH', fi, cnt, rng)
    # overlapping/duplicate clips (hellpig deaths) share frames, so verify run contiguity
    for (first, cnt, rng) in clipinfo:
        sl = want[first:first + cnt]
        for a, b in zip(sl, sl[1:]): assert fmap[b] == fmap[a] + 1, (spec['name'], sl)
    for i, t in thi: blob += rec(i, t)
    for i, t in tlo: blob += rec(i, t)
    blob += Qn.tobytes()
    blob += b'\0' * (-len(blob) % 4)
    if has_order:
        # hud guns: a draw order per stored frame, worked out with a depth buffer (gunorder.py).  Triangles that face away or
        # sit behind the near plane in that frame go first, so they are still drawn - behind everything - if the
        # in-between frames turn them round.
        tl = [(ren[t[0]], ren[t[1]], ren[t[2]]) for _, t in thi]; msi = int(round(mscale * 256))
        for f in range(len(uniq)):
            P = Qn[f].astype(float) * msi / 256.0
            front = gunorder.draw_order(P, tl); fs = set(front)
            order = [i for i in range(len(tl)) if i not in fs] + front
            blob += struct.pack('<%dH' % len(order), *order)
        blob += b'\0' * (-len(blob) % 4)
    # DMA (LoadImage) needs the skin 4-byte aligned
    sk, clut, q = skin(spec.get('skin') or spec['path'] + 'skin.jpg', spec.get('swap'), spec.get('lift'), spec.get('gen') == 'sphere')
    blob += sk + clut
    return blob, q, dict(name=spec['name'], verts=len(used), frames=len(uniq), t0=len(thi), t1=len(tlo), size=len(blob), mscale=mscale, ext=(allp.min(axis=(0, 1)), allp.max(axis=(0, 1))))

def main():
    out = sys.argv[1]
    only = set(sys.argv[2:])
    blobs, names, sheet = [], [], []
    for spec in MODELS:
        if only and spec['name'] not in only: continue
        b, q, info = emit(spec)
        blobs.append(b); names.append(spec['name']); sheet.append(q)
        print('%-10s verts %3d frames %3d tris %3d/%3d  %6d B  ext %s..%s' % (info['name'], info['verts'], info['frames'], info['t0'], info['t1'], info['size'], np.round(info['ext'][0]).astype(int), np.round(info['ext'][1]).astype(int)))
    hdr = b'MDL1' + struct.pack('<HH', len(blobs), 0)
    off = len(hdr) + 4 * len(blobs); offs = []
    for b in blobs: offs.append(off); off += len(b)
    with open(out, 'wb') as f:
        f.write(hdr); f.write(struct.pack('<%dI' % len(blobs), *offs))
        for b in blobs: f.write(b)
    open(out + '.names', 'w').write('\n'.join(names) + '\n')
    print('total', off, 'bytes,', len(blobs), 'models')

if __name__ == '__main__': main()
