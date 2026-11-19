#!/usr/bin/env python3
"""Convert Cube (2005_08_29) .cgz maps to the flat .CMP format used by the PS1 port.

CMP layout (little endian):
  char  magic[4]  = "CMP2"
  u8    sfactor, u8 numents_hi(0), u16 numents
  i16   waterlevel (cube units, clamped)
  char  title[64]
  u8    texlist[256]            (texture slot -> texture id, list 0)
  entity[numents]  = i16 x,y,z, i16 attr1, u8 type, u8 attr2, attr3, attr4   (12 bytes)
  cell[ssize*ssize] = u8 type, i8 floor, i8 ceil, u8 wtex, ftex, ctex, utex, vdelta, tag, r, g, b (12 bytes)
Cells are stored row-major, index = y*ssize + x, exactly as Cube's world[].
r,g,b is Cube's static lighting (worldlight.cpp calclight), computed here from the map's light entities: the value at a
cell lights the vertex at its top-left corner, and 128 is "texture at full brightness" (Cube draws the world with a x2
overbright, which is exactly how the PlayStation modulates a texture by a vertex colour).
Usage: cgz2cmp.py in.cgz out.cmp [--png out.png]
"""
import os, gzip, struct, sys

SOLID, CORNER, FHF, CHF, SPACE, SEMISOLID = 0, 1, 2, 3, 4, 5

def load(path):
    f = gzip.open(path, 'rb')
    data = f.read()
    p = 0
    head, version, headersize, sfactor, numents = struct.unpack_from('<4siiii', data, p)
    if head != b'CUBE': raise ValueError('not a cube map')
    if version > 5: raise ValueError('map version %d too new' % version)
    p += 20
    title = data[p:p+128].split(b'\0')[0].decode('latin1'); p += 128
    texlists = [list(data[p+256*i:p+256*(i+1)]) for i in range(3)]; p += 768
    if version >= 4:
        waterlevel = struct.unpack_from('<i', data, p)[0]; p += 4
        p += 4*15
    else:
        waterlevel = -100000
    ents = []
    for _ in range(numents):
        x, y, z, a1, t, a2, a3, a4 = struct.unpack_from('<hhhhBBBB', data, p); p += 12
        if t == 1:                       # LIGHT
            if not a2: a2 = 255
            if a1 > 32: a1 = 32
        ents.append((x, y, z, a1, t, a2, a3, a4))
    ssize = 1 << sfactor
    total = ssize * ssize
    cells = []
    prev = None
    def gb():
        nonlocal p
        v = data[p]; p += 1; return v
    def sb(v): return v - 256 if v > 127 else v
    while len(cells) < total:
        t = gb()
        if t == 255:
            n = gb()
            cells.extend([prev] * n)
            continue
        if t == 254:                     # v<=2 only
            gb(); gb(); cells.append(prev); continue
        if t == SOLID:
            wtex = gb(); vd = gb()
            if version <= 2: gb(); gb()
            c = (SOLID, 0, 16, wtex, 3, 4, wtex, vd, 0)
        else:
            if t >= 6: raise ValueError('bad cell type %d at %d' % (t, len(cells)))
            fl = sb(gb()); ce = sb(gb())
            wtex = gb(); ftex = gb(); ctex = gb()
            if version <= 2: gb(); gb()
            vd = gb()
            utex = gb() if version >= 2 else wtex
            tag = gb() if version >= 5 else 0
            c = (t, fl, ce, wtex, ftex, ctex, utex, vd, tag)
        cells.append(c); prev = c
    if len(cells) != total:
        raise ValueError('cell count mismatch %d != %d' % (len(cells), total))
    return dict(title=title, sfactor=sfactor, texlist=texlists[0], water=waterlevel, ents=ents, cells=cells)

AMBIENT_LIFT = 20

class MSRand:
    """rand() as in the Windows C runtime Cube was built with (the lighting noise is median filtered afterwards anyway)"""
    def __init__(self): self.s = 1
    def seed(self, v): self.s = v
    def rnd(self, n):
        self.s = (self.s * 214013 + 2531011) & 0xFFFFFFFF
        return ((self.s >> 16) & 0x7FFF) % n

def calclight(m):
    """Port of Cube's calclight / calclightsource / lightray / postlightarea (overbright path, lightscale 4)."""
    import math
    ss = 1 << m['sfactor']; cells = m['cells']; LS = 4; P = 12; PF = 4096.0
    ar, ag, ab = m.get('ambient', (10, 10, 10))                     # Cube's 10; our own maps may ask for more (their .cfg)
    R = [ar] * (ss * ss); G = [ag] * (ss * ss); B = [ab] * (ss * ss)
    solid = [c[0] == SOLID for c in cells]
    rng = MSRand()
    def outbord(x, y): return x < 2 or y < 2 or x >= ss - 2 or y >= ss - 2
    def lightray(bx, by, e):
        lx = e[0] + (rng.rnd(21) - 10) * 0.1; ly = e[1] + (rng.rnd(21) - 10) * 0.1
        dx = bx - lx; dy = by - ly; dist = math.sqrt(dx * dx + dy * dy)
        if dist < 1.0: return
        reach = e[3]; steps = int(reach * reach * 1.6 / dist)
        if steps <= 0: return
        x = int(lx * PF); y = int(ly * PF); l = e[5] << P
        stepx = int(dx / steps * PF); stepy = int(dy / steps * PF); stepl = int(l / float(steps))
        l //= LS; stepl = int(stepl / LS)
        if e[6] or e[7]:
            dim = rng.rnd((255 - (e[5] + e[6] + e[7]) // 3) // 16 + 1)
            x += stepx * dim; y += stepy * dim
            if outbord(x >> P, y >> P): return
            g = e[6] << P; stepg = int(g / float(steps)); b = e[7] << P; stepb = int(b / float(steps))
            g //= LS; stepg = int(stepg / LS); b //= LS; stepb = int(stepb / LS)
            for _ in range(steps):
                i = (y >> P) * ss + (x >> P)
                if i < 0 or i >= ss * ss: return
                R[i] = min(255, max(0, (l >> P) + R[i])); G[i] = min(255, max(0, (g >> P) + G[i])); B[i] = min(255, max(0, (b >> P) + B[i]))
                if solid[i]: return
                x += stepx; y += stepy; l -= stepl; g -= stepg; b -= stepb; stepl -= 25; stepg -= 25; stepb -= 25
        else:
            dim = rng.rnd((255 - e[5]) // 16 + 1)
            x += stepx * dim; y += stepy * dim
            if outbord(x >> P, y >> P): return
            for _ in range(steps):
                i = (y >> P) * ss + (x >> P)
                if i < 0 or i >= ss * ss: return
                v = min(255, max(0, (l >> P) + R[i])); R[i] = G[i] = B[i] = v
                if solid[i]: return
                x += stepx; y += stepy; l -= stepl; stepl -= 25
    for e in m['ents']:
        if e[4] != 1: continue
        reach = e[3]; sx, ex, sy, ey = e[0] - reach, e[0] + reach, e[1] - reach, e[1] + reach
        rng.seed(1); s = 0.8
        v = float(sx)
        while v <= ex: lightray(v, float(sy), e); lightray(v, float(ey), e); v += s * 2
        v = sy + s
        while v <= ey - s: lightray(float(sx), v, e); lightray(float(ex), v, e); v += s * 2
    for C in (R, G, B):                                # postlightarea: in-place smoothing, x outer / y inner as in Cube
        for x in range(1, ss - 1):
            for y in range(1, ss - 1):
                i = y * ss + x
                C[i] = (C[i] * 2 + C[i + 1] * 2 + C[i + ss] * 2 + C[i - 1] * 2 + C[i - ss] * 2 + C[i + ss + 1] + C[i - ss + 1] + C[i + ss - 1] + C[i - ss - 1]) // 14
    # the PlayStation's 15-bit colour crushes Cube's ambient level of 10 (8% brightness) to pure black: lift everything a little
    for C in (R, G, B):
        for i in range(len(C)): C[i] = min(255, C[i] + AMBIENT_LIFT)
    m['light'] = (R, G, B)

def write(m, path):
    with open(path, 'wb') as o:
        o.write(b'CMP2')
        o.write(struct.pack('<BBH', m['sfactor'], 0, len(m['ents'])))
        o.write(struct.pack('<h', max(-32768, min(32767, m['water']))))
        o.write(m['title'].encode('latin1')[:63].ljust(64, b'\0'))
        o.write(bytes(m['texlist']))
        for e in m['ents']: o.write(struct.pack('<hhhhBBBB', *e))
        R, G, B = m['light']
        for i, c in enumerate(m['cells']): o.write(struct.pack('<BbbBBBBBBBBB', *c, R[i], G[i], B[i]))

def png(m, path):
    from PIL import Image
    n = 1 << m['sfactor']
    im = Image.new('RGB', (n, n))
    for i, c in enumerate(m['cells']):
        if c[0] == SOLID: col = (20, 20, 20)
        else:
            h = max(0, min(255, 128 + c[1] * 6)); col = (h, h, h)
            if c[1] < m['water'] // 1 and False: col = (0, 0, 200)
        im.putpixel((i % n, i // n), col)
    for e in m['ents']:
        col = {2: (0,255,0), 14: (255,0,0)}.get(e[4])
        if col and 0 <= e[0] < n and 0 <= e[1] < n: im.putpixel((e[0], e[1]), col)
    im.resize((n*3, n*3), Image.NEAREST).save(path)

CARROT, ENDS_MAP = 16, 1

def mark_level_scripts(m, cgzpath):
    """Cube runs the map's cfg alias level_trigger_<tag> when a carrot fires.  The PS1 has no script engine, so the one
    thing those scripts do in the maps we use - `map <next>` - is recorded on the carrot as attr4 bit 0 (ends the level)."""
    import re
    cfg = os.path.splitext(cgzpath)[0] + '.cfg'
    if not os.path.exists(cfg): return []
    found = []
    for tag, body in re.findall(r'alias\s+level_trigger_(\d+)\s+\[([^\]]*)\]', open(cfg, errors='replace').read()):
        if re.search(r'(^|;|\s)map\s', body):
            found.append(int(tag))
    ents = []
    for e in m['ents']:
        x, y, z, a1, t, a2, a3, a4 = e
        if t == CARROT and a1 in found: a4 |= ENDS_MAP
        ents.append((x, y, z, a1, t, a2, a3, a4))
    m['ents'] = ents
    return found

def crop(m, x0, y0, size):
    """Keep the size x size window at (x0, y0) of a bigger map: the cells outside go, the window's border ring becomes
    solid (Cube's maps always have one), and entities move with the window (those outside it go).  pig06 is a 512 map
    whose rooms all stand in its top-left 256 corner; the rest is an empty field nobody can reach."""
    n = 1 << m['sfactor']; cells = m['cells']; out = []
    for y in range(size):
        for x in range(size):
            c = cells[(y0 + y) * n + x0 + x]
            if x in (0, size - 1) or y in (0, size - 1): c = (SOLID, 0, 16, c[3], 3, 4, c[3], 0, 0)
            out.append(c)
    ents = [(x - x0, y - y0) + e[2:] for e in m['ents'] for x, y in [e[:2]] if x0 < x < x0 + size - 1 and y0 < y < y0 + size - 1]
    print('crop %d,%d %dx%d: kept %d of %d entities' % (x0, y0, size, size, len(ents), len(m['ents'])))
    m['cells'], m['ents'], m['sfactor'] = out, ents, size.bit_length() - 1

CLIPS = 19
def add_clips(m):
    """Our pistol has no ammo in Cube's maps: put a box of pistol rounds beside every second ammo pickup, and one near
    the first player start.  A box goes in a free open cell 1-3 units from its partner, on the same floor height, away
    from other entities and well clear of teleporters (they fire within 4 units)."""
    n = 1 << m['sfactor']; cells = m['cells']; ents = m['ents']
    def ok(x, y, fl, strict=True):
        if not (1 <= x < n - 1 and 1 <= y < n - 1): return False
        for yy in ((y - 1, y, y + 1) if strict else (y,)):
            for xx in ((x - 1, x, x + 1) if strict else (x,)):  # the box (and, first choice, its neighbours) open and level
                c = cells[yy * n + xx]
                if c[0] == SOLID or c[1] != fl or c[2] - c[1] < 4: return False
        for e in ents + new:
            d2 = (e[0] - x) ** 2 + (e[1] - y) ** 2
            if e[4] in (12, 13) and d2 < 5 * 5: return False
            if e[4] not in (1, 14) and d2 < 1.5 ** 2: return False
            if e[4] == 14 and d2 < 2 * 2: return False
        return True
    new = []; partners = []; k = 0
    for e in ents:
        if 3 <= e[4] <= 6:
            if k % 2 == 0: partners.append(e)
            k += 1
    starts = [e for e in ents if e[4] == 2]
    if starts: partners.insert(0, starts[0])
    items = sum(1 for e in ents if 3 <= e[4] <= 11)
    for e in partners:
        if items + len(new) >= 120: break
        c = cells[e[1] * n + e[0]] if 0 <= e[0] < n and 0 <= e[1] < n else None
        if not c or c[0] == SOLID: continue
        for strict, r in ((1, 2), (1, 1), (1, 3), (0, 2), (0, 1), (0, 3)):
            spot = next(((e[0] + dx, e[1] + dy) for dx, dy in ((r, 0), (-r, 0), (0, r), (0, -r), (r, r), (-r, -r), (r, -r), (-r, r))
                         if ok(e[0] + dx, e[1] + dy, c[1], strict)), None)
            if spot: new.append((spot[0], spot[1], e[2], 0, CLIPS, 0, 0, 0)); break
    m['ents'] = ents + new
    print('pistol ammo boxes: %d placed (%d partners)' % (len(new), len(partners)))

if __name__ == '__main__':
    if len(sys.argv) < 3: print(__doc__); sys.exit(1)
    m = load(sys.argv[1])
    if '--crop' in sys.argv:
        x0, y0, sz = (int(v) for v in sys.argv[sys.argv.index('--crop') + 1].split(','))
        crop(m, x0, y0, sz)
    if '--no-clips' not in sys.argv: add_clips(m)
    cfgp = os.path.splitext(sys.argv[1])[0] + '.cfg'
    if os.path.exists(cfgp):                                       # '// ambient r g b' (ours: Cube itself skips the comment)
        import re
        am = re.search(r'^//\s*ambient\s+(\d+)\s+(\d+)\s+(\d+)', open(cfgp, errors='replace').read(), re.M)
        if am: m['ambient'] = tuple(int(v) for v in am.groups()); print('ambient light', m['ambient'])
    calclight(m); ends = mark_level_scripts(m, sys.argv[1]); write(m, sys.argv[2])
    if ends: print('level_trigger scripts that change map (carrot ends the level): tags', ends)
    if '--png' in sys.argv: png(m, sys.argv[sys.argv.index('--png') + 1])
    solid = sum(1 for c in m['cells'] if c[0] == SOLID)
    print('%s: "%s" %dx%d, %d ents, %d%% solid' % (sys.argv[1], m['title'], 1<<m['sfactor'], 1<<m['sfactor'], len(m['ents']), 100*solid//len(m['cells'])))
