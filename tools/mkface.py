#!/usr/bin/env python3
"""mkface.py - the status bar portrait for Cube PSX ("the dude").

Drawn here in code as 16-colour pixel art: broad jaw, slicked dark hair, aviators with eyes that show through the tinted
lenses, a cigar with a live ember.  The game builds the face out of layers so it can animate cheaply:

  5 full heads 44x50          health >= 75, >= 50, >= 25, < 25, dead
  eye strips   EYE_W x EYE_H  look centre / left / right / squint (pain) / blink; replace the lens band of any head
  mouths       MOU_W x MOU_H  cigar / gritted teeth (pain) / smirk (kill); replace the mouth band of any head
  embers       5x4            dim / bright cigar tip
  smoke        6x6 x 3        puffs that drift up with semi-transparency

All of it fits a 256x128 4bpp block (texture page (960,256), v 128..255) with one CLUT.

usage: mkface.py out.bin [--sheet preview.png]   (out.bin = 16384 bytes of pixels + 32 bytes CLUT + layout table)"""
import struct, sys, math
from PIL import Image, ImageDraw

W, H = 44, 54

# palette (index: rgb).  0 = transparent
PAL = [
    (0, 0, 0),          # 0 transparent
    (8, 6, 10),         # 1 outline / pupils / shirt
    (92, 52, 40),       # 2 skin shadow
    (150, 92, 64),      # 3 skin mid
    (200, 136, 96),     # 4 skin light
    (238, 184, 140),    # 5 skin highlight
    (34, 24, 22),       # 6 hair
    (66, 52, 48),       # 7 hair sheen
    (226, 178, 62),     # 8 gold (frames, cigar band)
    (36, 52, 60),       # 9 lens
    (150, 190, 200),    # 10 lens glare / smoke
    (150, 150, 140),    # 11 eye whites through the tint
    (84, 46, 26),       # 12 cigar dark
    (136, 84, 46),      # 13 cigar light
    (232, 84, 30),      # 14 ember / blood bright
    (255, 232, 120),    # 15 ember hot
]
BLOOD = 14
T, K, S0, S1, S2, S3, HR, HS, GD, LN, GL, EW, C0, C1, EM, EH = range(16)

# layer rectangles inside a head (the game pastes eye / mouth strips here)
EYE_X, EYE_Y, EYE_W, EYE_H = 9, 13, 26, 17
MOU_X, MOU_Y, MOU_W, MOU_H = 15, 31, 29, 13
EMB_X, EMB_Y = 37, 39            # ember sprite (5x4) position in head coordinates


class Canvas:
    def __init__(s, w=W, h=H):
        s.w, s.h = w, h
        s.p = [[T] * w for _ in range(h)]

    def get(s, x, y):
        return s.p[y][x] if 0 <= x < s.w and 0 <= y < s.h else T

    def put(s, x, y, c):
        if 0 <= x < s.w and 0 <= y < s.h: s.p[y][x] = c

    def poly(s, pts, c, mask=None):
        im = Image.new('L', (s.w, s.h), 0)
        ImageDraw.Draw(im).polygon([(x, y) for x, y in pts], fill=1)
        for y in range(s.h):
            for x in range(s.w):
                if im.getpixel((x, y)) and (mask is None or mask(x, y)): s.p[y][x] = c

    def ellipse(s, cx, cy, rx, ry, c, mask=None):
        for y in range(s.h):
            for x in range(s.w):
                if ((x + .5 - cx) / rx) ** 2 + ((y + .5 - cy) / ry) ** 2 <= 1 and (mask is None or mask(x, y)): s.p[y][x] = c

    def line(s, pts, c):
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            n = max(abs(x1 - x0), abs(y1 - y0), 1)
            for i in range(int(n) + 1):
                s.put(int(round(x0 + (x1 - x0) * i / n)), int(round(y0 + (y1 - y0) * i / n)), c)

    def copy(s):
        c = Canvas(s.w, s.h); c.p = [r[:] for r in s.p]; return c

    def crop(s, x, y, w, h):
        c = Canvas(w, h)
        for j in range(h):
            for i in range(w): c.p[j][i] = s.get(x + i, y + j)
        return c


SKIN = {S0, S1, S2, S3}


def mirror(pts):
    return [(44 - x, y) for x, y in pts]


# ---- the head: narrow temples, high cheekbones, hollow cheeks, then a long jaw out to hard corners and a square chin ----
HEAD = [(14, 6), (30, 6), (33.5, 9), (35, 15), (35.5, 22), (36.2, 27), (35.2, 31), (36.6, 36), (36.9, 39), (34.5, 42.5),
        (29.5, 46.5), (27.5, 47.6), (16.5, 47.6), (14.5, 46.5), (9.5, 42.5), (7.1, 39), (7.4, 36), (8.8, 31), (7.8, 27),
        (8.5, 22), (9, 15), (10.5, 9)]
HAIR = [(8.8, 21), (8.2, 12), (9, 6), (12, 2.5), (17, 0.6), (26, 0), (32, 1.5), (35.5, 5), (36, 11), (35.3, 21), (34, 21),
        (33.6, 13), (31, 10.4), (26, 9.6), (21, 11.2), (17, 11), (14.5, 14.6), (13, 11.6), (10.6, 14.5), (10.2, 21)]
BROW_L = [(10.6, 16.6), (15, 15.8), (20.8, 18.2), (20.4, 19.8), (15, 18.2), (11, 18.6)]
LENS_L = [(9.6, 20.5), (20.6, 20.5), (21.4, 22.5), (21, 26), (18.6, 28.6), (14.6, 28.8), (11.4, 27.2), (9.8, 23.5)]
LENS_R = mirror(LENS_L)


def skinmask(c):
    return lambda x, y: c.p[y][x] in SKIN


def base_head():
    c = Canvas()
    # bull neck and traps, black tank top
    c.poly([(13, 38), (31, 38), (32, 48), (44, 51), (44, 54), (0, 54), (0, 51), (12, 48)], S2)
    c.poly([(26, 38), (31, 38), (32, 48), (44, 51), (44, 54), (30, 54)], S1)
    c.poly([(33, 49), (44, 51.5), (44, 54), (38, 54)], S0)
    c.poly([(12, 42), (32, 42), (30.5, 49.5), (13.5, 49.5)], S1)                        # shade under the jaw
    c.line([(16, 47), (18, 50)], S1); c.line([(28, 47), (26, 50)], S0)                  # neck cords
    c.poly([(5, 54), (13, 50.4), (22, 53.2), (31, 50.4), (39, 54)], K)                  # tank top
    # ears
    c.ellipse(8, 25, 2.3, 4.4, S2); c.ellipse(36, 25, 2.3, 4.4, S1)
    c.line([(7, 23), (7, 27)], S1); c.line([(37, 23), (37, 27)], S0)
    # the face: flat mid tone, then light and shadow planes (light from the upper left)
    c.poly(HEAD, S2)
    m = skinmask(c)
    c.poly([(29, 7), (34, 12), (35.5, 22), (36.2, 27), (33, 31), (31, 25), (31, 13)], S1, mask=m)       # right side plane
    c.poly([(35.2, 31), (36.9, 39), (34.5, 42.5), (29.5, 46.5), (30, 37)], S1, mask=m)                  # right jaw plane
    c.poly([(9.4, 31), (13.6, 32.6), (13, 34.6), (9.6, 34)], S1, mask=m)                                # left cheek hollow
    c.poly([(34.6, 31), (30.4, 32.6), (31, 34.6), (34.4, 34)], S0, mask=m)                              # right cheek hollow
    c.poly([(15, 12), (19, 11.5), (17, 14), (14.5, 14.5)], S3, mask=m)                                    # forehead light
    c.poly([(9.5, 29), (14.5, 28.8), (14.5, 30.4), (9.5, 31)], S3, mask=m)                               # left cheekbone
    c.poly([(17.6, 42.6), (26.4, 42.6), (25.6, 45.6), (18.4, 45.6)], S3, mask=m)                         # chin
    c.poly([(8, 36), (10, 36), (11, 41), (9, 40)], S3, mask=m)                                            # left jaw corner
    return c


def stubble(c):
    """five o'clock shadow: an even, sparse dot screen over the jaw and the upper lip"""
    hm = Canvas(); hm.poly(HEAD, 1)
    for y in range(33, 48):
        for x in range(6, 39):
            v = c.p[y][x]
            if v not in (S1, S2, S3) or not hm.p[y][x]: continue
            jaw = y >= 39 or (y >= 34 and (x < 14 or x > 30))
            lip = 34 <= y <= 35 and 18 <= x <= 26
            if (jaw or lip) and y % 2 == 1 and x % 2 == (y // 2) % 2:
                c.p[y][x] = {S3: S2, S2: S1, S1: S0}[v]


def features(c, brow=0):
    stubble(c)
    c.line([(7, 39), (9.5, 42.5), (14.5, 46.5), (16.5, 47.6), (27.5, 47.6), (29.5, 46.5), (34.5, 42.5), (37, 39)], K)   # jaw ink
    c.line([(22, 44), (22, 46)], S1); c.put(22, 43, S2)                                 # chin cleft
    # nose: long straight bridge lit on the left, shadow on the right, wide nostrils
    c.line([(21, 23), (21, 30)], S3); c.line([(23, 22), (23, 30)], S1); c.line([(24, 27), (25, 31)], S0)
    c.put(22, 31, S3); c.put(21, 31, S3)
    c.line([(19, 32), (20, 33)], S1); c.line([(25, 33), (26, 32)], S0)
    c.line([(20, 33), (24, 33)], S1); c.put(20, 33, S0); c.put(24, 33, S0)
    # hair: slicked-back pompadour with a lock falling on the brow
    c.poly(HAIR, HR)
    for pts in [[(10, 7), (14, 3.5), (21, 2), (28, 2)], [(12, 9), (17, 5), (25, 4), (31, 4.5), (34, 7)], [(16, 9.5), (21, 7.5), (29, 7)],
                [(13, 12), (14, 9)]]:
        c.line(pts, HS)
    if brow:                                                                             # angry furrow
        c.line([(20, 14), (21, 16)], S1); c.line([(24, 14), (23, 16)], S1)
    c.poly(BROW_L, HR); c.poly(mirror(BROW_L), HR)
    return c


def glasses(c, look=0, mode='open'):
    """look -1/0/1 = pupils left/centre/right; mode open / squint / blink"""
    c.poly(LENS_L, LN); c.poly(LENS_R, LN)
    for cx in (15, 29):
        if mode == 'open':
            for x in range(cx - 3, cx + 3): c.put(x, 24, EW)
            for x in range(cx - 2, cx + 2): c.put(x, 23, EW); c.put(x, 25, EW)
            px = cx - 1 + look * 2
            for dx in (0, 1):
                for y in (23, 24): c.put(px + dx, y, K)
        elif mode == 'squint':
            if cx < 22: c.line([(cx - 3, 23), (cx + 2, 25)], EW); c.line([(cx - 2, 24), (cx + 1, 25)], K)
            else: c.line([(cx - 3, 25), (cx + 2, 23)], EW); c.line([(cx - 2, 25), (cx + 1, 24)], K)
        else:
            c.line([(cx - 3, 24), (cx + 2, 24)], K)
    for ox in (0, 14):                                                                   # glare
        c.line([(11 + ox, 25), (14 + ox, 21)], GL)
        c.put(19 + ox, 26, GL)
    for poly in (LENS_L, LENS_R):
        pts = [(int(round(x)), int(round(y))) for x, y in poly]
        c.line(pts + [pts[0]], GD)
    c.line([(10, 20), (34, 20)], GD); c.line([(21, 22), (23, 22)], GD)                     # brow bar and bridge
    c.line([(9, 21), (7, 22)], GD); c.line([(35, 21), (37, 22)], GD)                     # arms
    return c


def mouth(c, kind='cigar'):
    if kind == 'cigar':
        c.line([(17, 37), (27, 37)], K); c.put(16, 38, K)                               # hard, flat mouth
        c.line([(18, 39), (25, 39)], S3)
        cigar(c, 27, 37)
    elif kind == 'grit':                                                                 # hit: teeth bared, cigar clamped
        c.poly([(16, 35), (29, 35), (28, 40), (17, 40)], K)
        c.line([(17, 36), (27, 36)], EW); c.line([(17, 39), (27, 39)], EW)
        for x in (19, 22, 25): c.put(x, 36, K); c.put(x, 39, K)
        cigar(c, 28, 37)
    elif kind == 'smirk':                                                                # kill: one corner goes up
        c.line([(17, 38), (22, 38), (26, 37), (28, 35)], K)
        c.line([(18, 39), (24, 39)], S3); c.put(29, 36, S1)
        cigar(c, 27, 36, up=1)
    elif kind == 'none':                                                                 # base heads: the mouth layer adds the rest
        pass
    elif kind == 'dead':
        c.line([(17, 38), (27, 38)], K); c.put(18, 37, K); c.put(26, 37, K)
    return c


def cigar(c, x, y, up=0):
    """a fat cigar out of the corner of the mouth, tilted down (up=1: tilted up, jaunty)"""
    dy = -1 if up else 1
    pts = [(x + i, y + (i * dy) // 4) for i in range(13)]
    for i, (px, py) in enumerate(pts):
        if i > 1: c.put(px, py - 2, K); c.put(px, py + 2, K)
        c.put(px, py - 1, C1); c.put(px, py, C1 if i % 3 else C0); c.put(px, py + 1, C0)
    for i in (2, 3):
        px, py = pts[i]
        for d in (-1, 0, 1): c.put(px, py + d, GD)                                       # band
    ex, ey = pts[-1]
    c.put(ex + 1, ey - 1, K); c.put(ex + 1, ey, EM); c.put(ex + 1, ey + 1, K); c.put(ex + 2, ey, K); c.put(ex, ey, EM)
    return c


def outline(c):
    o = c.copy()
    for y in range(H):
        for x in range(W):
            if c.p[y][x] == T and any(c.get(x + dx, y + dy) not in (T, K) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                o.p[y][x] = K
    return o


def in_band(x, y):
    return (EYE_X <= x < EYE_X + EYE_W and EYE_Y <= y < EYE_Y + EYE_H) or (MOU_X <= x < MOU_X + MOU_W and MOU_Y <= y < MOU_Y + MOU_H)


def blood(c, level):
    """battle damage, kept clear of the eye and mouth bands (which get swapped at run time)"""
    def ok(x, y):
        return c.p[y][x] in SKIN and not in_band(x, y)
    marks = []
    if level >= 1: marks += [[(15, 10), (16, 11), (17, 11)], [(36, 30), (35, 31)]]
    if level >= 2: marks += [[(27, 8), (28, 9), (28, 10), (29, 11), (29, 12)], [(10, 30), (10, 31), (11, 32), (11, 33)],
                             [(33, 44), (32, 45)]]
    if level >= 3: marks += [[(18, 8), (18, 9), (19, 10), (19, 11), (19, 12)], [(33, 28), (34, 29), (34, 30), (35, 31), (35, 32)],
                             [(12, 42), (13, 43), (13, 44)], [(8, 33), (8, 34)], [(25, 45), (26, 45), (26, 46)]]
    for mk in marks:
        for x, y in mk:
            if ok(x, y): c.p[y][x] = BLOOD
    if level >= 2:                                                                       # bruise
        for y in range(29, 35):
            for x in range(8, 13):
                if ok(x, y) and c.p[y][x] in (S2, S3): c.p[y][x] = S1
    return c


def head(level, look=0, emode='open', mkind='cigar'):
    c = base_head(); features(c, brow=(emode == 'squint' or mkind == 'grit'))
    glasses(c, look, emode); mouth(c, mkind)
    c = outline(c)
    blood(c, level)
    return c


def dead_head():
    c = base_head(); features(c)
    glasses(c, 0, 'blink'); mouth(c, 'dead')
    c.line([(25, 21), (28, 24), (27, 26), (31, 28)], GL)                                # cracked lens
    c = outline(c)
    blood(c, 3)
    for x, y in [(20, 34), (20, 35), (21, 36), (24, 35), (24, 36), (14, 42), (15, 43), (30, 42), (29, 43), (22, 40), (22, 41)]:
        if c.p[y][x] in SKIN: c.p[y][x] = BLOOD
    return c


def smoke(n):
    c = Canvas(6, 6)
    r = [1.4, 2.2, 2.9][n]
    c.ellipse(3, 3, r, r * .85, GL)
    if n == 2:
        c.put(2, 2, T); c.put(3, 3, T)
    return c


def ember(bright):
    c = Canvas(5, 4)
    if bright:
        for x, y in [(1, 1), (2, 1), (1, 2), (2, 2)]: c.put(x, y, EH)
        for x, y in [(3, 1), (3, 2), (0, 1), (0, 2), (1, 0), (2, 0), (1, 3), (2, 3)]: c.put(x, y, EM)
    else:
        for x, y in [(1, 1), (2, 1), (1, 2), (2, 2)]: c.put(x, y, EM)
    return c


# ---- the sheet: 256x128 ----
def build():
    sheet = Canvas(256, 128)
    layout = {}

    def place(name, cv, x, y):
        for j in range(cv.h):
            for i in range(cv.w):
                sheet.p[y + j][x + i] = cv.p[j][i]
        layout[name] = (x, y, cv.w, cv.h)

    heads = [head(l, 0, 'open', 'none') for l in range(4)] + [dead_head()]
    base = head(0, 0, 'open', 'none')
    for v in [head(0, -1), head(0, 1), head(0, 0, 'squint', 'grit'), head(0, 0, 'blink'), head(0, 0, 'open', 'smirk'), head(0)]:
        for y in range(H):
            for x in range(W):
                if v.p[y][x] != base.p[y][x] and not in_band(x, y): raise SystemExit('variant differs outside the bands at %d,%d' % (x, y))
    for i, h in enumerate(heads): place('head%d' % i, h, i * 44, 0)
    ref = head(0)
    eyes = [head(0, 0), head(0, -1), head(0, 1), head(0, 0, 'squint', 'grit'), head(0, 0, 'blink')]
    for i, e in enumerate(eyes): place('eye%d' % i, e.crop(EYE_X, EYE_Y, EYE_W, EYE_H), i * EYE_W, 56)
    mouths = [head(0, 0, 'open', 'cigar'), head(0, 0, 'squint', 'grit'), head(0, 0, 'open', 'smirk')]
    for i, m in enumerate(mouths): place('mouth%d' % i, m.crop(MOU_X, MOU_Y, MOU_W, MOU_H), i * MOU_W, 74)
    for i in range(2): place('ember%d' % i, ember(i), 90 + i * 6, 74)
    for i in range(3): place('smoke%d' % i, smoke(i), 102 + i * 7, 74)
    # sanity: nothing on the sheet overlaps
    rects = list(layout.values())
    for i, a in enumerate(rects):
        for b in rects[:i]:
            if a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]: raise SystemExit('sheet overlap %s %s' % (a, b))
        if a[0] + a[2] > 256 or a[1] + a[3] > 128: raise SystemExit('off the sheet %s' % (a,))
    # sanity: the strips must leave a head unchanged when pasted back (so they line up exactly)
    return sheet, layout, heads, eyes, mouths


def rgb(i):
    return PAL[i]


def to_image(cv, scale=1, bg=(40, 40, 48)):
    im = Image.new('RGB', (cv.w, cv.h))
    im.putdata([bg if v == T else PAL[v] for row in cv.p for v in row])
    return im.resize((cv.w * scale, cv.h * scale), Image.NEAREST)


def c555(r, g, b):
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def pack():
    sheet, layout, heads, eyes, mouths = build()
    out = bytearray()
    for row in sheet.p:
        for x in range(0, 256, 2): out.append(row[x] | (row[x + 1] << 4))
    # bit 15 on the glare / smoke colour: on the PS1 a textured pixel only blends in a see-through primitive when it is set
    clut = b''.join(struct.pack('<H', 0 if i == 0 else (c555(*PAL[i]) | (0x8000 if i == GL else 0)) or 0x8000) for i in range(16))
    out += clut
    order = ['head0', 'head1', 'head2', 'head3', 'head4', 'eye0', 'eye1', 'eye2', 'eye3', 'eye4', 'mouth0', 'mouth1', 'mouth2',
             'ember0', 'ember1', 'smoke0', 'smoke1', 'smoke2']
    for n in order: out += struct.pack('<4B', *layout[n])
    out += struct.pack('<6B', EYE_X, EYE_Y, MOU_X, MOU_Y, EMB_X, EMB_Y)
    out += b'\0' * ((-len(out)) % 4)
    return bytes(out)


def main():
    sheet, layout, heads, eyes, mouths = build()
    out = pack()
    open(sys.argv[1], 'wb').write(out)
    print('%s: %d bytes' % (sys.argv[1], len(out)))
    if '--sheet' in sys.argv:
        p = sys.argv[sys.argv.index('--sheet') + 1]
        big = Image.new('RGB', (W * 8 * 5 + 40, H * 8 * 2 + 30 + 256 * 0), (20, 20, 24))
        for i, h in enumerate(heads): big.paste(to_image(h, 8), (i * (W * 8 + 8), 0))
        # composites: looks, pain, smirk
        demo = [head(0, -1), head(0, 1), head(1, 0, 'squint', 'grit'), head(2, 0, 'open', 'smirk'), head(3, 0, 'blink')]
        for i, h in enumerate(demo): big.paste(to_image(h, 8), (i * (W * 8 + 8), H * 8 + 20))
        big.save(p)
        to_image(sheet, 3).save(p.replace('.png', '_atlas.png'))


if __name__ == '__main__': main()
