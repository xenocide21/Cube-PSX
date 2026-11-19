"""Parse a Cube map .cfg: returns list of texture paths indexed by slot (frame 0 only)."""
import re
def parse_cfg(path, textures=None):
    tex = [] if textures is None else textures
    for line in open(path, encoding='latin1'):
        line = line.split('//')[0].strip()
        if not line: continue
        if line.startswith('texturereset'): tex.clear(); continue
        m = re.match(r'texture\s+(\d+)\s+"([^"]+)"', line)
        if m:
            if int(m.group(1)) == 0: tex.append(m.group(2))
            else: pass           # animation frames are skipped; they don't take a slot
    return tex
