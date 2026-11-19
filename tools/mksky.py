#!/usr/bin/env python3
"""mksky.py <basename> <out.pak>: pack Cube's six skybox faces (ft bk lf rt dn up) as 128x128 4bpp textures ('SKY1' + 6 x (8192 px + 32 clut))."""
import sys, struct
from PIL import Image
base, out = sys.argv[1], sys.argv[2]
buf = b'SKY1'
for side in ('ft', 'bk', 'lf', 'rt', 'dn', 'up'):
    im = Image.open('/home/claude/cube_tgz/cube/packages/%s_%s.jpg' % (base, side)).convert('RGB').resize((128, 128), Image.LANCZOS)
    q = im.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = (q.getpalette() or [])[:48]; pal += [0] * (48 - len(pal)); px = q.tobytes(); packed = bytearray()
    for i in range(0, len(px), 2): packed.append((px[i] & 15) | ((px[i + 1] & 15) << 4))
    def c555(r, g, b):
        v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10); return v or 0x0421
    buf += bytes(packed) + b''.join(struct.pack('<H', c555(*pal[i * 3:i * 3 + 3])) for i in range(16))
open(out, 'wb').write(buf); print(out, len(buf))
