#!/usr/bin/env python3
"""xmap.py - a small library for building Cube maps (.cgz version 5 + .cfg) by hand-written layout, for the PS1-exclusive
map pack (tools/mkxeno.py).  The world starts solid; rooms, corridors, doors (tagged cells a carrot opens), pools, steps,
pillars and open sky are carved out of it, and entities are placed in cell coordinates.  check() plays the map through
on its grid: from the start, walking (steps of 1, jumps of up to 4 units), dropping and swimming, opening each carrot's
doors as it is reached, until the exit carrot - and checks every space is wide enough for a body (3 cells)."""
import os, struct, gzip
import numpy as np

SOLID, CORNER, FHF, CHF, SPACE = 0, 1, 2, 3, 4
LIGHT, PLAYERSTART, SHELLS, BULLETS, ROCKETS, ROUNDS, HEALTH, BOOST, GREENARMOUR, YELLOWARMOUR, QUAD = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
TELEPORT, TELEDEST, MAPMODEL, MONSTER, CARROT, JUMPPAD = 12, 13, 14, 15, 16, 17
FLAG, CLIPS, HILL = 18, 19, 20
OGRO, RHINO, RATA, SLITH, BAUUL, HELLPIG, KNIGHT, GOBLIN = range(8)
PACKAGES = '/home/claude/cube_tgz/cube/packages'

class Map:
    def __init__(self, size, title, textures, water=None, ambient=(44, 44, 44)):
        self.N = size; self.title = title; self.tex = textures; self.water = water; self.ambient = ambient
        n = size
        self.T = np.full((n, n), SOLID, np.int32); self.F = np.zeros((n, n), np.int32); self.C = np.full((n, n), 16, np.int32)
        self.W = np.full((n, n), 2, np.int32); self.FT = np.full((n, n), 3, np.int32); self.CT = np.full((n, n), 4, np.int32)
        self.U = np.full((n, n), 2, np.int32); self.TAG = np.zeros((n, n), np.int32)
        self.ents = []; self.exits = {}          # exit tag -> next map ("" = the end)
        self.notes = []
        for t in textures:
            assert os.path.exists(os.path.join(PACKAGES, t)), t

    # ---- cells: rectangles are [x0, x1) x [y0, y1) ----
    def _r(self, x0, y0, x1, y1):
        return slice(max(0, y0), min(self.N, y1)), slice(max(0, x0), min(self.N, x1))
    def room(self, x0, y0, x1, y1, f=0, c=14, w=None, ft=None, ct=None, sky=False, u=None):
        """an open space; sky=True: no ceiling (texture 0 shows the sky box), the walls go up to the sky height"""
        s = self._r(x0, y0, x1, y1)
        self.T[s] = SPACE; self.F[s] = f; self.C[s] = (f + 64 if sky else c); self.TAG[s] = 0
        if w is not None: self.W[s] = w; self.U[s] = w if u is None else u
        if ft is not None: self.FT[s] = ft
        if ct is not None or sky: self.CT[s] = 0 if sky else ct
        self.wallring(x0, y0, x1, y1, w)
    def wallring(self, x0, y0, x1, y1, w):
        """the solid cells around a room take its wall texture (a solid cell's walls show its own wtex)"""
        if w is None: return
        for x in range(x0 - 1, x1 + 1):
            for y in (y0 - 1, y1):
                if 0 <= x < self.N and 0 <= y < self.N and self.T[y, x] == SOLID: self.W[y, x] = w
        for y in range(y0, y1):
            for x in (x0 - 1, x1):
                if 0 <= x < self.N and 0 <= y < self.N and self.T[y, x] == SOLID: self.W[y, x] = w
    def floor(self, x0, y0, x1, y1, f, ft=None):
        s = self._r(x0, y0, x1, y1); self.F[s] = f
        if ft is not None: self.FT[s] = ft
    def ceil(self, x0, y0, x1, y1, c, ct=None):
        s = self._r(x0, y0, x1, y1); self.C[s] = c
        if ct is not None: self.CT[s] = ct
    def solid(self, x0, y0, x1, y1, w=None):
        s = self._r(x0, y0, x1, y1); self.T[s] = SOLID; self.TAG[s] = 0
        if w is not None: self.W[s] = w
    def wall(self, x0, y0, x1, y1, w=None, gaps=()):
        """a dividing wall (solid) across a room, with openings: gaps = (from, to) along its long side"""
        along_x = (x1 - x0) >= (y1 - y0)
        for k in range(x0, x1) if along_x else range(y0, y1):
            if any(a <= k < b for (a, b) in gaps): continue
            if along_x: self.solid(k, y0, k + 1, y1, w)
            else: self.solid(x0, k, x1, k + 1, w)
    def door(self, x0, y0, x1, y1, tag, f=None, c=None, w=None):
        """cells a carrot with this tag opens (solid until then).  Floor / ceiling: as given, else the room's around"""
        s = self._r(x0, y0, x1, y1)
        if f is None: f = self._near(x0, y0, x1, y1, self.F, max)
        if c is None: c = self._near(x0, y0, x1, y1, self.C, min)
        self.T[s] = SPACE; self.F[s] = f; self.C[s] = c; self.TAG[s] = tag
        if w is not None: self.W[s] = w; self.U[s] = w
    def _near(self, x0, y0, x1, y1, A, pick):
        v = []
        for x in range(x0 - 1, x1 + 1):
            for y in range(y0 - 1, y1 + 1):
                if 0 <= x < self.N and 0 <= y < self.N and self.T[y, x] != SOLID and not self.TAG[y, x] and not (x0 <= x < x1 and y0 <= y < y1): v.append(int(A[y, x]))
        return pick(v) if v else 0
    def corridor(self, ax, ay, bx, by, wdt=3, f=0, c=10, w=None, ft=None, ct=None, sky=False):
        """an L: first along x from (ax,ay) to bx, then along y to by; wdt cells wide, (ax,ay) at its top-left"""
        x0, x1 = min(ax, bx), max(ax, bx)
        self.room(x0, ay, x1 + wdt, ay + wdt, f, c, w, ft, ct, sky)
        y0, y1 = min(ay, by), max(ay, by)
        self.room(bx, y0, bx + wdt, y1 + wdt, f, c, w, ft, ct, sky)
    def pool(self, x0, y0, x1, y1, f, ft=None):
        """a sunken floor: under the water if the map has water above f"""
        self.floor(x0, y0, x1, y1, f, ft)
    def stairs(self, x0, y0, x1, y1, f0, f1, axis='x', ft=None):
        """one unit a step, from f0 at the x0 / y0 end to f1"""
        n = (x1 - x0) if axis == 'x' else (y1 - y0)
        for k in range(n):
            f = f0 + round((f1 - f0) * (k + 1) / n)
            if axis == 'x': self.floor(x0 + k, y0, x0 + k + 1, y1, f, ft)
            else: self.floor(x0, y0 + k, x1, y0 + k + 1, f, ft)

    # ---- entities (cell coordinates; z: the floor there unless given) ----
    def _z(self, x, y, z): return int(self.F[y, x]) if z is None else z
    def ent(self, x, y, t, a1=0, a2=0, a3=0, a4=0, z=None):
        self.ents.append((int(x), int(y), self._z(int(x), int(y), z), int(a1), t, int(a2), int(a3), int(a4)))
    def start(self, x, y, yaw=0): self.ent(x, y, PLAYERSTART, yaw)
    def light(self, x, y, r=12, rgb=(255, 255, 255), z=None):
        self.ent(x, y, LIGHT, r, rgb[0], rgb[1], rgb[2], z=(self._z(x, y, None) + 6) if z is None else z)
    def mon(self, x, y, kind, yaw=0): self.ent(x, y, MONSTER, yaw, kind)
    def item(self, x, y, kind): self.ent(x, y, kind)
    def carrot(self, x, y, tag, exit_to=None):
        self.ent(x, y, CARROT, tag)
        if exit_to is not None: self.exits[tag] = exit_to
    def tele(self, x, y, tag): self.ent(x, y, TELEPORT, tag)
    def dest(self, x, y, tag, yaw=0): self.ent(x, y, TELEDEST, yaw, tag)
    def pad(self, x, y, up, py=0, px=0): self.ent(x, y, JUMPPAD, up, py, px)
    def flag(self, x, y, team): self.ent(x, y, FLAG, 0, team)
    def hill(self, x, y, hx, hy): self.ent(x, y, HILL, hx, hy)

    # ---- output ----
    def finish(self):
        n = self.N; b = 2
        self.T[:b, :] = SOLID; self.T[-b:, :] = SOLID; self.T[:, :b] = SOLID; self.T[:, -b:] = SOLID
    def write(self, path, mapdir):
        self.finish()
        n = self.N; sf = n.bit_length() - 1
        title = self.title.encode()[:127]
        wl = self.water if self.water is not None else -100000
        hdr = struct.pack('<4siiii', b'CUBE', 5, 20 + 128 + 768 + 4 + 60, sf, len(self.ents))
        hdr += title.ljust(128, b'\0') + bytes(range(256)) * 3 + struct.pack('<i', wl) + b'\0' * 60
        body = bytearray()
        for e in self.ents: body += struct.pack('<hhhhBBBB', *e)
        T, F, C, W, FT, CT, U, TAG = self.T, self.F, self.C, self.W, self.FT, self.CT, self.U, self.TAG
        for y in range(n):
            for x in range(n):
                t = int(T[y, x])
                if t == SOLID: body += bytes([SOLID, int(W[y, x]), 0])
                else: body += bytes([t, int(F[y, x]) & 255, int(C[y, x]) & 255, int(W[y, x]), int(FT[y, x]), int(CT[y, x]), 0, int(U[y, x]), int(TAG[y, x])])
        with gzip.open(path, 'wb') as f: f.write(hdr + bytes(body))
        with open(os.path.splitext(path)[0] + '.cfg', 'w') as f:
            f.write('// %s - Cube PSX map pack by Xenocide21 (made by tools/mkxeno.py)\n// ambient %d %d %d\ntexturereset\n' % ((self.title,) + tuple(self.ambient)))
            for t in self.tex: f.write('texture 0 "%s"\n' % t)
            for tag, nxt in sorted(self.exits.items()):
                f.write('alias level_trigger_%d [map %s/%s]\n' % (tag, mapdir, nxt or 'end'))

    def lights(self, x0, y0, x1, y1, rgb=(255, 255, 255), every=9, r=None):
        """a grid of lights over a room, every `every` cells"""
        r = r or every + 8
        nx = max(1, (x1 - x0) // every); ny = max(1, (y1 - y0) // every)
        for i in range(nx):
            for j in range(ny):
                x = x0 + (x1 - x0) * (2 * i + 1) // (2 * nx); y = y0 + (y1 - y0) * (2 * j + 1) // (2 * ny)
                if self.T[y, x] != SOLID: self.light(x, y, r, rgb)

    def preview(self, path, reach=None):
        """a top-down picture: floor height as brightness, water blue, doors red (tag), sky cells purple-tinted, ents as dots"""
        from PIL import Image, ImageDraw
        n = self.N; k = 4 if n <= 128 else 2
        im = Image.new('RGB', (n * k, n * k), (0, 0, 0)); d = ImageDraw.Draw(im)
        W = self.water if self.water is not None else -1000
        for y in range(n):
            for x in range(n):
                if self.T[y, x] == SOLID: continue
                f = int(self.F[y, x]); v = max(30, min(230, 110 + f * 6))
                col = (v, v, v)
                if f < W: col = (30, 60, 160)
                if self.CT[y, x] == 0: col = (col[0] * 3 // 4 + 40, col[1] * 3 // 4, col[2] * 3 // 4 + 50)
                if self.TAG[y, x]: col = (200, 40, 40)
                d.rectangle([x * k, y * k, x * k + k - 1, y * k + k - 1], fill=col)
        cols = {PLAYERSTART: (0, 255, 0), MONSTER: (255, 120, 0), CARROT: (255, 0, 255), TELEPORT: (0, 255, 255), TELEDEST: (0, 140, 140),
                JUMPPAD: (255, 255, 0), LIGHT: None}
        for e in self.ents:
            c = cols.get(e[4], (255, 255, 255))
            if c: d.ellipse([e[0] * k - 2, e[1] * k - 2, e[0] * k + 2, e[1] * k + 2], fill=c)
        im.save(path)

    # ---- the play-through check ----
    def check(self, verbose=False):
        """walk the map from the first start: open doors as their carrots are reached; returns (ok, report)"""
        self.finish()
        n = self.N; T, F, C, TAG = self.T, self.F, self.C, self.TAG
        W = self.water if self.water is not None else -1000
        opened = set()
        def openc(x, y): return T[y, x] != SOLID and (TAG[y, x] == 0 or TAG[y, x] in opened)
        def standable(x, y):
            if not (2 <= x < n - 2 and 2 <= y < n - 2) or not openc(x, y): return False
            f = F[y, x]
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if not openc(x + dx, y + dy): return False
                    if C[y + dy, x + dx] - max(f, F[y + dy, x + dx]) < 4: return False
                    if F[y + dy, x + dx] > f + 1: return False          # something to bump into: stand next to it instead
            return True
        starts = [e for e in self.ents if e[4] == PLAYERSTART]
        if not starts: return False, 'no player start'
        sx, sy = starts[0][0], starts[0][1]
        if not standable(sx, sy): return False, 'the start (%d,%d) is too tight' % (sx, sy)
        carrots = [(e[0], e[1], e[3]) for e in self.ents if e[4] == CARROT]
        teles = {}
        for e in self.ents:
            if e[4] == TELEDEST: teles.setdefault(e[5], (e[0], e[1]))
        tps = [(e[0], e[1], e[3]) for e in self.ents if e[4] == TELEPORT]
        pads = [(e[0], e[1], e[3]) for e in self.ents if e[4] == JUMPPAD]
        order = []; reach = None
        while True:
            reach = np.zeros((n, n), bool); q = [(sx, sy)]; reach[sy, sx] = True
            while q:
                x, y = q.pop()
                f = F[y, x]; inwater = f < W
                nb = [(x + 1, y, 0), (x - 1, y, 0), (x, y + 1, 0), (x, y - 1, 0)]
                for (ddx, ddy) in ((2, 0), (-2, 0), (0, 2), (0, -2)):        # off a ledge, or up onto one: a stride past its edge
                    mx, my = x + ddx // 2, y + ddy // 2
                    if openc(mx, my): nb.append((x + ddx, y + ddy, 0))
                for (tx, ty, tag) in tps:                                 # teleporters: from within 2 cells
                    if abs(tx - x) <= 1 and abs(ty - y) <= 1 and tag in teles: nb.append(teles[tag] + (1,))
                for (px, py, up) in pads:                                 # a jump pad: anywhere up to its lift within 4 cells
                    if abs(px - x) <= 1 and abs(py - y) <= 1:
                        lift = min((up - 14) * 0.8, C[y, x] - f - 4)        # the game's pad (attr 20: 6 units, 30: 13), under the ceiling
                        for yy in range(y - 4, y + 5):
                            for xx in range(x - 4, x + 5):
                                if 0 <= xx < n and 0 <= yy < n and F[yy, xx] <= f + lift: nb.append((xx, yy, 1))
                for (bx, by, free) in nb:
                    if not (0 <= bx < n and 0 <= by < n) or reach[by, bx] or not standable(bx, by): continue
                    up = F[by, bx] - (max(f, W - 1) if inwater else f)     # in water you climb out from the surface
                    if up > 4 and not free: continue
                    reach[by, bx] = True; q.append((bx, by))
            new = [c for c in carrots if reach[c[1], c[0]] and c[2] not in opened]
            if not new: break
            for c in new: opened.add(c[2]); order.append(c[2])
        exits = [c for c in carrots if c[2] in self.exits]
        mons = [e for e in self.ents if e[4] == MONSTER]
        unreached_m = [(e[0], e[1]) for e in mons if not any(reach[e[1] + dy, e[0] + dx] for dy in (-2, -1, 0, 1, 2) for dx in (-2, -1, 0, 1, 2))]
        unopened = sorted(set(int(t) for t in np.unique(TAG) if t) - opened)
        ok = bool(exits) and all(reach[c[1], c[0]] for c in exits) or (not exits and not self.exits)
        rep = 'carrots in order %s; exit %s; monsters %d (%d out of reach); doors never opened %s; open cells %d, reached %d' % (
            order, 'reached' if ok else 'NOT REACHED', len(mons), len(unreached_m), unopened, int((T != SOLID).sum()), int(reach.sum()))
        if unreached_m and verbose: rep += '\n   out of reach: %s' % unreached_m[:10]
        if unopened: ok = False
        return ok, rep
