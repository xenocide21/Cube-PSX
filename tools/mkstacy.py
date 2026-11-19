#!/usr/bin/env python3
"""mkstacy.py - Giga Stacy, the second status bar portrait: the dude's counterpart.

Same build as mkface.py (and the same sheet layout, so the game draws her the same way): 16-colour pixel art in layers -
5 heads by health (and dead), eye strips (centre / left / right / squint / blink), mouth strips (smile / gritted teeth /
smirk).  The same features turned the other way: big wavy blonde hair over her shoulders, sculpted brows, gold aviators
with lashes showing through the tint, red lips, gold hoops, a strong jaw, broad shoulders, a black sports top.  No cigar:
her ember and smoke sprites are empty (the game knows not to puff).

usage: mkstacy.py out.bin [--sheet preview.png]"""
import struct, sys
from PIL import Image
import mkface
from mkface import Canvas, W, H, T, K, S0, S1, S2, S3, GD, LN, GL, EW, EYE_X, EYE_Y, EYE_W, EYE_H, MOU_X, MOU_Y, MOU_W, MOU_H, \
    EMB_X, EMB_Y, in_band, blood, c555

PAL = [
    (0, 0, 0),          # 0 transparent
    (10, 6, 10),        # 1 outline / pupils / liner / sports top
    (110, 62, 46),      # 2 skin shadow
    (172, 110, 80),     # 3 skin mid
    (214, 152, 114),    # 4 skin light
    (244, 198, 160),    # 5 skin highlight
    (118, 76, 34),      # 6 hair shadow (and brows)
    (204, 154, 70),     # 7 hair gold
    (226, 178, 62),     # 8 gold (frames, hoops)
    (44, 50, 66),       # 9 lens
    (160, 196, 210),    # 10 lens glare
    (160, 156, 150),    # 11 eye whites through the tint / teeth / lip gloss
    (122, 18, 34),      # 12 lip dark
    (196, 38, 56),      # 13 lip red
    (232, 84, 30),      # 14 blood
    (250, 226, 150),    # 15 hair highlight
]
HD, HM, HL, LD, LR = 6, 7, 15, 12, 13
SKIN = {S0, S1, S2, S3}


def mirror(pts):
    return [(44 - x, y) for x, y in pts]


# a narrower, tapered jaw to a defined chin; high cheekbones
HEAD = [(15, 6), (29, 6), (32.5, 9), (34, 15), (34.5, 22), (34.8, 27), (34, 32), (33.5, 36), (31.5, 40.5), (27.5, 44.5),
        (24.5, 46.2), (19.5, 46.2), (16.5, 44.5), (12.5, 40.5), (10.5, 36), (10, 32), (9.2, 27), (9.5, 22), (10, 15), (11.5, 9)]
HAIR_BACK = [(10, 4), (16, 0), (28, 0), (35, 3), (39, 9), (41, 17), (42, 26), (43.5, 36), (44, 45), (41.5, 50), (37, 47.5),
             (35, 40), (35.6, 28), (34.6, 14), (30, 9), (14, 9), (9.4, 14), (8.4, 28), (9, 40), (7, 47.5), (2.5, 50), (0, 45),
             (0.5, 36), (2, 26), (3, 17), (5, 9)]
HAIR_TOP = [(10.4, 16), (10, 9), (13, 4.5), (18, 2), (26, 1.6), (31, 3.8), (34, 8), (34.6, 17), (33.2, 14.5), (31.2, 11.2),
            (28, 9.4), (23, 9.8), (18.5, 12.2), (15, 15.2), (12.4, 18.6)]
LOCK_L = [(9.6, 14), (12.4, 17), (11.8, 25), (11.2, 33), (10.2, 40), (8, 46.5), (6.4, 40), (6.8, 30), (7.6, 21)]
LOCK_R = [(34.4, 14), (35.6, 20), (36.6, 30), (37.4, 40), (36.2, 46.5), (33.8, 40), (33.2, 32), (32.8, 24), (32.4, 18)]
BROW_L = [(11, 17.6), (14, 15.4), (18, 15.2), (20.6, 16.8), (20.3, 17.9), (17.6, 16.7), (14.2, 16.9), (11.4, 18.6)]
LENS_L = [(9.2, 19.8), (20.6, 20.4), (21.4, 22.5), (21, 26), (18.6, 28.6), (14.6, 28.8), (11.2, 27.2), (9.4, 23.2)]
LENS_R = mirror(LENS_L)


def skinmask(c):
    return lambda x, y: c.p[y][x] in SKIN


def base_head():
    c = Canvas()
    c.poly(HAIR_BACK, HD)                                                                 # the mass of hair behind
    c.poly([(7, 28), (37, 28), (39, 50), (5, 50)], HD)                                    # (down behind the neck)
    for y in range(4, 50):                                                                # waves: the outer edge bulges and tucks
        for side in (-1, 1):
            bump = [0, 1, 1, 0, -1][(y // 3) % 5]
            if bump > 0:
                x = (1 if side < 0 else 42) + (0 if side < 0 else 0)
                xs = [x - 1] if side < 0 else [x + 1]
                for xx in xs:
                    if 0 <= xx < 44 and y > 14 and c.get(xx + (1 if side < 0 else -1), y) == HD: c.put(xx, y, HD)
    for pts in [[(6, 12), (4, 22), (3, 32), (2, 42)], [(38, 12), (40, 22), (41, 32), (42, 42)], [(8, 20), (5.5, 34), (4.5, 46)],
                [(37, 20), (39, 34), (40, 46)]]:
        c.line(pts, HM)
    for pts in [[(5, 26), (3.5, 34), (4, 40), (2.5, 47)], [(39, 26), (40.5, 34), (40, 40), (41.5, 47)]]:
        c.line(pts, HL)
    # neck and strong shoulders, a black sports top (halter straps, a high band)
    c.poly([(16.5, 40), (27.5, 40), (28.5, 47), (40, 49.5), (44, 51.5), (44, 54), (0, 54), (0, 51.5), (4, 49.5), (15.5, 47)], S2)
    c.poly([(25, 40), (27.5, 40), (28.5, 47), (40, 49.5), (44, 51.5), (44, 54), (30, 54)], S1)
    c.poly([(16, 42), (28, 42), (27, 46.5), (17, 46.5)], S1)                              # shade under the jaw
    c.line([(3, 50), (8, 49)], S3); c.line([(36, 49), (41, 50)], S0)                      # deltoids
    c.line([(13, 50), (19, 49.5)], S1); c.line([(25, 49.5), (31, 50)], S0)                # collarbones
    c.poly([(9, 54), (12.6, 51.4), (22, 53.6), (31.4, 51.4), (35, 54)], K)                # the top
    c.line([(12, 52), (17, 47)], K); c.line([(32, 52), (27, 47)], K)                      # straps
    # the face
    c.poly(HEAD, S2)
    m = skinmask(c)
    c.poly([(29, 7), (33, 12), (34.5, 22), (34.8, 27), (32, 31), (30.6, 25), (30.6, 13)], S1, mask=m)   # right side plane
    c.poly([(34, 32), (33.5, 36), (31.5, 40.5), (27.5, 44.5), (29, 36)], S1, mask=m)                   # right jaw plane
    c.poly([(10.4, 31.6), (14.4, 32.6), (13.6, 34.4), (10.8, 34.2)], S1, mask=m)                      # cheek contour
    c.poly([(33.6, 31.6), (29.6, 32.6), (30.4, 34.4), (33.2, 34.2)], S1, mask=m)
    c.poly([(16, 11), (21, 10.5), (18.5, 13.5), (15.5, 14)], S3, mask=m)                              # forehead light
    c.poly([(10.2, 29), (15.4, 28.8), (15, 30.6), (10.4, 31)], S3, mask=m)                            # cheekbones
    c.poly([(19, 42.4), (25, 42.4), (24.4, 45), (19.6, 45)], S3, mask=m)                              # chin
    return c


def features(c, brow=0):
    c.line([(10.5, 36), (12.5, 40.5), (16.5, 44.5), (19.5, 46.2), (24.5, 46.2), (27.5, 44.5), (31.5, 40.5), (33.5, 36)], S0)  # jaw: a soft edge, not ink
    # nose: a fine straight bridge, a small tip
    c.line([(21, 24), (21, 29)], S3); c.line([(23, 24), (23, 30)], S1)
    c.put(22, 31, S3); c.put(21, 31, S3); c.put(20, 32, S1); c.put(24, 32, S0); c.line([(21, 32), (23, 32)], S1)
    c.put(30, 34, K)                                                                       # beauty mark
    # hair over the top: a deep side part, the sweep falling to the left brow, locks down both cheeks
    c.poly(HAIR_TOP, HM); c.poly(LOCK_L, HM); c.poly(LOCK_R, HD)
    for pts in [[(11, 9), (15, 5), (21, 3), (27, 3)], [(13, 12), (18, 8), (24, 6.5)], [(28, 3), (31, 6), (33, 11)],
                [(9, 20), (9.4, 30), (8, 42)], [(11, 18), (10.4, 26)]]:
        c.line(pts, HL)
    for pts in [[(16, 14), (20, 11), (26, 10)], [(35, 18), (36, 30), (36.4, 42)], [(27, 4), (29, 8.6)]]:
        c.line(pts, HD)
    if brow:
        c.line([(20, 15), (21, 16)], S1); c.line([(24, 15), (23, 16)], S1)
    c.poly(BROW_L, HD); c.poly(mirror(BROW_L), HD)
    # gold hoops under the hair
    for cx in (9, 35):
        for x, y in [(cx - 1, 35), (cx, 35), (cx + 1, 36), (cx + 1, 37), (cx, 38), (cx - 1, 38), (cx - 2, 37), (cx - 2, 36)]:
            c.put(x, y, GD)
    return c


def glasses(c, look=0, mode='open'):
    """aviators with the eyes showing through: lined, with a flick of lashes at the outer corners"""
    c.poly(LENS_L, LN); c.poly(LENS_R, LN)
    for cx, side in ((15, -1), (29, 1)):
        oc = cx - 4 if side < 0 else cx + 3                                                # outer corner
        if mode == 'open':
            for x in range(cx - 3, cx + 3): c.put(x, 24, EW)
            for x in range(cx - 2, cx + 2): c.put(x, 25, EW)
            c.line([(cx - 3, 23), (cx + 2, 23)], K)                                          # upper liner
            c.put(oc, 22, K); c.put(oc + side, 21, K)                                        # the wing
            px = cx - 1 + look * 2
            for dx in (0, 1):
                for y in (24, 25): c.put(px + dx, y, K)
        elif mode == 'squint':
            c.line([(cx - 3, 24), (cx + 2, 24)], K); c.line([(cx - 2, 25), (cx + 1, 25)], EW)
            c.put(oc, 23, K)
        else:                                                                                # blink: lid down, lashes
            c.line([(cx - 3, 24), (cx + 2, 24)], K)
            for x in range(cx - 3, cx + 3, 2): c.put(x, 25, K)
            c.put(oc, 23, K)
    for ox in (0, 14):                                                                     # glare, clear of the eyes
        c.line([(11 + ox, 22), (12 + ox, 21)], GL)
        c.line([(17 + ox, 28), (19 + ox, 27)], GL)
    for poly in (LENS_L, LENS_R):
        pts = [(int(round(x)), int(round(y))) for x, y in poly]
        c.line(pts + [pts[0]], GD)
    c.line([(21, 22), (23, 22)], GD)
    c.line([(9, 21), (7, 22)], GD); c.line([(35, 21), (37, 22)], GD)
    return c


def lips(c, top, bottom, corners_up=(0, 0)):
    """full lips: an upper lip with a bow, the line, a fuller lower lip with a glint"""
    l, r = corners_up
    c.put(19, top, LD); c.put(20, top, LD); c.put(24, top, LD); c.put(25, top, LD)
    c.line([(18, top + 1), (26, top + 1)], LR); c.put(22, top + 1, LD)
    c.line([(17, top + 2), (27, top + 2)], K); c.put(16, top + 2 - l, K); c.put(28, top + 2 - r, K)
    c.line([(18, bottom), (26, bottom)], LR); c.line([(19, bottom + 1), (25, bottom + 1)], LD)
    c.put(21, bottom, EW)


def mouth(c, kind='smile'):
    if kind == 'smile':                                                                    # closed, confident, the corners up
        lips(c, 35, 38, (1, 1))
        c.put(15, 34, S1); c.put(29, 34, S0)
    elif kind == 'grit':                                                                   # hit: teeth bared
        c.poly([(16, 35), (28, 35), (27, 40), (17, 40)], K)
        c.line([(17, 36), (27, 36)], EW); c.line([(18, 39), (26, 39)], EW)
        for x in (19, 22, 25): c.put(x, 36, K); c.put(x, 39, K)
        c.line([(17, 34), (27, 34)], LR); c.line([(18, 41), (26, 41)], LR); c.put(16, 35, LD); c.put(28, 35, LD)
    elif kind == 'smirk':                                                                  # kill: a grin, teeth showing
        c.put(19, 34, LD); c.put(20, 34, LD); c.put(24, 34, LD); c.put(25, 34, LD)
        c.line([(18, 35), (27, 35)], LR)
        c.line([(18, 36), (27, 36)], EW); c.put(17, 36, K); c.put(28, 35, K); c.put(29, 34, K)
        c.line([(18, 37), (27, 37)], K)
        c.line([(19, 38), (26, 38)], LR); c.line([(20, 39), (25, 39)], LD); c.put(22, 38, EW)
    elif kind == 'dead':
        c.line([(18, 36), (26, 36)], LD); c.line([(17, 37), (27, 37)], K); c.line([(18, 38), (26, 38)], LD)
    return c


def outline(c):
    o = c.copy()
    for y in range(H):
        for x in range(W):
            if c.p[y][x] == T and any(c.get(x + dx, y + dy) not in (T, K) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                o.p[y][x] = K
    return o


def head(level, look=0, emode='open', mkind='smile'):
    c = base_head(); features(c, brow=(emode == 'squint' or mkind == 'grit'))
    glasses(c, look, emode); mouth(c, mkind)
    c = outline(c)
    blood(c, level)
    return c


def dead_head():
    c = base_head(); features(c)
    glasses(c, 0, 'blink'); mouth(c, 'dead')
    c.line([(25, 21), (28, 24), (27, 26), (31, 28)], GL)
    c = outline(c)
    blood(c, 3)
    for x, y in [(14, 41), (15, 42), (30, 41), (29, 42), (22, 41), (22, 42)]:
        if c.p[y][x] in SKIN: c.p[y][x] = 14
    return c


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
    eyes = [head(0, 0), head(0, -1), head(0, 1), head(0, 0, 'squint', 'grit'), head(0, 0, 'blink')]
    for i, e in enumerate(eyes): place('eye%d' % i, e.crop(EYE_X, EYE_Y, EYE_W, EYE_H), i * EYE_W, 56)
    mouths = [head(0, 0, 'open', 'smile'), head(0, 0, 'squint', 'grit'), head(0, 0, 'open', 'smirk')]
    for i, m in enumerate(mouths): place('mouth%d' % i, m.crop(MOU_X, MOU_Y, MOU_W, MOU_H), i * MOU_W, 74)
    for i in range(2): place('ember%d' % i, Canvas(1, 1), 90 + i * 6, 74)                  # (no cigar)
    for i in range(3): place('smoke%d' % i, Canvas(1, 1), 102 + i * 7, 74)
    return sheet, layout, heads, eyes, mouths


def to_image(cv, scale=1, bg=(40, 40, 48)):
    im = Image.new('RGB', (cv.w, cv.h))
    im.putdata([bg if v == T else PAL[v] for row in cv.p for v in row])
    return im.resize((cv.w * scale, cv.h * scale), Image.NEAREST)


def pack():
    sheet, layout, heads, eyes, mouths = build()
    out = bytearray()
    for row in sheet.p:
        for x in range(0, 256, 2): out.append(row[x] | (row[x + 1] << 4))
    out += b''.join(struct.pack('<H', 0 if i == 0 else (c555(*PAL[i]) | (0x8000 if i == GL else 0)) or 0x8000) for i in range(16))
    order = ['head0', 'head1', 'head2', 'head3', 'head4', 'eye0', 'eye1', 'eye2', 'eye3', 'eye4', 'mouth0', 'mouth1', 'mouth2',
             'ember0', 'ember1', 'smoke0', 'smoke1', 'smoke2']
    for n in order: out += struct.pack('<4B', *layout[n])
    out += struct.pack('<6B', EYE_X, EYE_Y, MOU_X, MOU_Y, EMB_X, EMB_Y)
    out += b'\0' * ((-len(out)) % 4)
    return bytes(out)


def main():
    out = pack()
    open(sys.argv[1], 'wb').write(out)
    print('%s: %d bytes' % (sys.argv[1], len(out)))
    if '--sheet' in sys.argv:
        p = sys.argv[sys.argv.index('--sheet') + 1]
        sheet, layout, heads, eyes, mouths = build()
        big = Image.new('RGB', (W * 8 * 5 + 40, H * 8 * 2 + 20), (20, 20, 24))
        for i, h in enumerate(heads): big.paste(to_image(h, 8), (i * (W * 8 + 8), 0))
        demo = [head(0, -1), head(0, 1), head(1, 0, 'squint', 'grit'), head(2, 0, 'open', 'smirk'), head(3, 0, 'blink')]
        for i, h in enumerate(demo): big.paste(to_image(h, 8), (i * (W * 8 + 8), H * 8 + 20))
        big.save(p)


if __name__ == '__main__': main()
