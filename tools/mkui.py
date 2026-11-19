#!/usr/bin/env python3
"""mkui.py - menu / splash assets for Cube PSX -> maps/ui.pak

  Cube's own font (data/newchars.png, 64 px glyphs, proportional, coordinates from rendertext.cpp char_coords) shrunk
  to 16 px: a 128x128 4bpp atlas whose 16 grey levels are tinted by the vertex colour at draw time.
  The Cube logo (128x128, 16 colours) for the splash screen and the menu cursor.
  A 16x16 16-colour icon for the memory card save files.

UI02 layout (little endian):
  "UI02", u16 0, u16 0
  font:  8192 bytes 4bpp 128x128 (low nibble = left pixel), 32 bytes CLUT, 94 x {u8 x0, y0, x1, y1} for chars 33..126
  logo:  8192 bytes 4bpp 128x128, 32 bytes CLUT
  icon:  128 bytes 4bpp 16x16, 32 bytes CLUT
  face:  the status bar portrait (tools/mkface.py): 16384 bytes 4bpp 256x128, 32 bytes CLUT, 18 x {u8 x, y, w, h} sprite
         rectangles, u8 eye x/y, mouth x/y, ember x/y offsets inside a head, padding to 4 bytes
usage: mkui.py out.pak [--sheet preview.png]"""
import re, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkface
from PIL import Image

CUBE = '/home/claude/cube_tgz/cube'
SRC = '/home/claude/cube_zip/cube_source/src/rendertext.cpp'
LOGO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'cube_logo.png')


def c555(r, g, b, stp=0):
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (stp << 15)


def pack4(idx, w, h):
    out = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            out.append((idx[y * w + x] & 15) | ((idx[y * w + x + 1] & 15) << 4))
    return bytes(out)


def font():
    """Cube's layout shrunk 4x, but each glyph is shrunk on its own with everything outside its box blanked, so a glyph
    never picks up a neighbour's pixels where their boxes share an atlas column (L's foot used to show left of M)"""
    coords = [tuple(int(v) for v in t) for t in re.findall(r'\{(\d+),(\d+),(\d+),(\d+)\}', open(SRC).read().replace(' ', ''))]
    assert len(coords) == 94, len(coords)
    src = Image.open(os.path.join(CUBE, 'data', 'newchars.png')).convert('L')
    im = Image.new('L', (128, 128), 0)
    for x0, y0, x1, y1 in coords:                                  # left to right: a shared column ends up blank-edged
        bx0, bx1, by0, by1 = x0 // 4, (x1 + 3) // 4, y0 // 4, (y1 + 3) // 4
        g = Image.new('L', ((bx1 - bx0) * 4, (by1 - by0) * 4), 0)
        g.paste(src.crop((x0, y0, x1, y1)), (x0 - bx0 * 4, y0 - by0 * 4))
        im.paste(g.resize((bx1 - bx0, by1 - by0), Image.LANCZOS), (bx0, by0))
    px = list(im.getdata())
    idx = [min(15, (v + 8) // 17) for v in px]                 # 0 = transparent, 1..15 = grey ramp
    clut = b''.join(struct.pack('<H', 0 if i == 0 else c555(i * 17, i * 17, i * 17)) for i in range(16))
    glyphs = b''.join(struct.pack('<4B', x0 // 4, y0 // 4, (x1 + 3) // 4, (y1 + 3) // 4) for x0, y0, x1, y1 in coords)
    return pack4(idx, 128, 128) + clut + glyphs, im


def quant16(im, w, h, keep_black=True):
    im = im.convert('RGBA').resize((w, h), Image.LANCZOS)
    bg = Image.new('RGBA', im.size, (0, 0, 0, 255)); bg.alpha_composite(im)
    q = bg.convert('RGB').quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = (q.getpalette() or [])[:48]; pal += [0] * (48 - len(pal))
    # colour 0x0000 is transparent on the PS1: black pixels get 0x8000 (opaque black) instead
    clut = b''.join(struct.pack('<H', c555(*pal[i * 3:i * 3 + 3]) or 0x8000) for i in range(16))
    return pack4(list(q.getdata()), w, h) + clut, q.convert('RGB')


def main():
    out = sys.argv[1]
    f, fim = font()
    logo = Image.open(LOGO)
    l, lim = quant16(logo, 128, 128)
    ic, iim = quant16(logo, 16, 16)
    with open(out, 'wb') as o:
        o.write(b'UI02' + struct.pack('<HH', 0, 0)); o.write(f); o.write(l); o.write(ic); o.write(mkface.pack())
    print('%s: %d bytes' % (out, os.path.getsize(out)))
    if '--sheet' in sys.argv:
        s = Image.new('RGB', (128 * 3 + 64, 128))
        s.paste(fim.convert('RGB'), (0, 0)); s.paste(lim, (128, 0)); s.paste(logo.convert('RGB'), (256, 0)); s.paste(iim.resize((64, 64), Image.NEAREST), (384, 0))
        s.resize((s.width * 2, 256), Image.NEAREST).save(sys.argv[sys.argv.index('--sheet') + 1])


if __name__ == '__main__': main()
