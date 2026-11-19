#!/usr/bin/env python3
"""Build the PS1 texture pack (.tex) for one Cube map.

usage: mktex.py map.cgz out.tex [--packages DIR] [--data DIR] [--sheet out.png]

Cube picks textures by slot number: slots come from `texture 0 "file"` lines, first in data/default_map_settings.cfg and then in
the map's own .cfg (a `texturereset` clears the list). Cube maps texture coordinates at 32 texels per world unit, so an image
of W x H pixels repeats every W/32 x H/32 units; we keep that and store each image as 128x128 4bpp (16-colour CLUT).

TEX3 layout (little endian):
  "TEX3", u16 count, u16 0
  A map with water also gets its liquid texture (Cube's DEFAULT_LIQUID, slot 1) as an extra 16-colour entry with slot
  number 255, last in the file, whose colours all have the PS1's semi-transparency bit set.
  count x { u8 slot, u8 log2(tile width units), u8 log2(tile height units), u8 bits per pixel (8 or 4),
            8 bpp: 16384 bytes 128x128, one byte per pixel;  4 bpp: 8192 bytes, low nibble = left pixel,
            CLUT: 256 or 16 x u16 (15-bit RGB, never 0 so no texel is transparent) }
Video memory has room for 48 quarter-page units of world texture; an 8-bit texture takes two.  The textures the map uses
most get 256 colours, and the rest 16 if the map has more than 24 textures (they come first in the file).
"""
import argparse, collections, importlib.util, os, struct, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cubecfg
spec = importlib.util.spec_from_file_location('cgz2cmp', os.path.join(HERE, 'cgz2cmp.py'))
cgz = importlib.util.module_from_spec(spec); spec.loader.exec_module(cgz)

TEX = 128
UNITS = 48           # VRAM budget in quarter pages (12 pages); MAXTEX textures at most
MAXTEX = 48

def slot_usage(cells):
    use = collections.Counter()
    for c in cells:
        if c[0] == cgz.SOLID: use[c[3]] += 1
        else:
            for k in (3, 4, 5, 6): use[c[k]] += 1
    return use

def log2(n):
    return max(0, n.bit_length() - 1)

def to555(r, g, b):
    v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
    return v or 0x0421

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cgz'); ap.add_argument('out')
    ap.add_argument('--packages', default='/home/claude/cube_tgz/cube/packages')
    ap.add_argument('--data', default='/home/claude/cube_tgz/cube/data')
    ap.add_argument('--sheet')
    a = ap.parse_args()
    m = cgz.load(a.cgz)
    tex = cubecfg.parse_cfg(os.path.join(a.data, 'default_map_settings.cfg'))
    cfg = os.path.splitext(a.cgz)[0] + '.cfg'
    if os.path.exists(cfg): tex = cubecfg.parse_cfg(cfg, tex)
    tex = tex[:256]
    use = slot_usage(m['cells'])
    entries, skipped = [], []
    for slot, n in use.most_common():
        if slot == 0: continue                           # the sky is not a texture: it shows the sky box
        path = os.path.join(a.packages, tex[slot]) if slot < len(tex) else None
        if not path or not os.path.exists(path): skipped.append(slot); continue
        if len(entries) >= MAXTEX: skipped.append(slot); continue
        im = Image.open(path).convert('RGB')
        entries.append((slot, im, path, n))
    water = m['water'] > -128 and len(tex) > 1 and os.path.exists(os.path.join(a.packages, tex[1]))
    if water:
        if len(entries) >= MAXTEX: entries.pop()
        entries.append((255, Image.open(os.path.join(a.packages, tex[1])).convert('RGB'), os.path.join(a.packages, tex[1]), 0))
    # map props: the skins of the mapmodels this map places go in its pack too, as 16-colour slots 240 + prop number
    import mkmodel
    props = sorted({e[5] for e in m['ents'] if e[4] == 14 and e[5] < len(mkmodel.MAPMODELS)} | ({mkmodel.MAPMODELS.index('flag')} if any(e[4] == 18 for e in m['ents']) else set()) | {mkmodel.MAPMODELS.index('clips')})   # the pistol ammo box: every map (cgz2cmp adds them)
    n8 = max(0, min(len(entries) - (1 if water else 0), UNITS - len(entries) - len(props)))
    out = []
    for k, (slot, im, path, n) in enumerate(entries):
        w, h = im.size; bpp = 8 if k < n8 and slot != 255 else 4
        small = im.resize((TEX, TEX), Image.LANCZOS)
        if bpp == 8:
            q = small.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            pal = (q.getpalette() or [])[:768]; pal += [0] * (768 - len(pal))
            packed = q.tobytes()
        else:
            q = small.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
            pal = (q.getpalette() or [])[:48]; pal += [0] * (48 - len(pal))
            px = q.tobytes(); packed = bytearray()
            for i in range(0, len(px), 2): packed.append((px[i] & 15) | ((px[i + 1] & 15) << 4))
        clut = b''.join(struct.pack('<H', to555(*pal[i * 3:i * 3 + 3]) | (0x8000 if slot == 255 else 0)) for i in range(len(pal) // 3))
        tw = min(16, max(1, w // 32)); th = min(16, max(1, h // 32))
        out.append((slot, log2(tw), log2(th), bpp, bytes(packed), clut, q, path, n))
    for k in props:
        sk, clut, q = mkmodel.skin(mkmodel.mapskin(k))
        out.insert(len(out) - (1 if water else 0), (240 + k, 2, 2, 4, sk, clut, q, mkmodel.MAPMODELS[k], 0))
    if props: print('props:', ', '.join(mkmodel.MAPMODELS[k] for k in props))
    entries = out
    with open(a.out, 'wb') as o:
        o.write(b'TEX3' + struct.pack('<HH', len(entries), 0))
        for slot, lw, lh, bpp, packed, clut, *_ in entries:
            o.write(struct.pack('<BBBB', slot, lw, lh, bpp)); o.write(packed); o.write(clut)
    if water: print('water level %d: liquid texture %s' % (m['water'], tex[1]))
    print('%s: %d textures, %d with 256 colours (%d bytes), %d slots skipped %s' % (os.path.basename(a.out), len(entries), sum(1 for e in entries if e[3] == 8), os.path.getsize(a.out), len(skipped), skipped[:8]))
    if a.sheet:
        cols = 8; rows = (len(entries) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * 130, max(1, rows) * 130), (30, 30, 30))
        for i, (slot, lw, lh, bpp, packed, clut, q, path, n) in enumerate(entries):
            qq = q.convert('RGB')
            sheet.paste(qq, ((i % cols) * 130, (i // cols) * 130))
        sheet.save(a.sheet)

if __name__ == '__main__':
    main()
