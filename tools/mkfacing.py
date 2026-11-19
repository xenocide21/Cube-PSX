#!/usr/bin/env python3
"""mkfacing.py - "Facing Cubes", a Cube remake of Unreal Tournament's Facing Worlds for CTF.
Writes a real Cube map (.cgz, version 5) and its .cfg (texture list), so the usual tools (cgz2cmp.py, mktex.py) take it.

Cube's world is a height field: a cell has one floor and one ceiling, so the temple's levels cannot be stacked - they are
stepped instead.  Each temple (32 wide, 28 deep) faces the bridge:
  hall (ground): entrance in the middle of the front; ahead the pillar (8x8, its way in from the back, the teleporter to
      the roof inside); on the left the teleporter to level 2 (near the door) and to level 3 (at the back); on the right
      the flag.  A teleporter fires within 4 units (Cube), so they stand well apart.
  roof (48): the front half of both flanks, either side of the tall middle - the rifle (Cube's sniper gun).
  level 3 (32): the left flank behind the roof - the chaingun.   level 2 (16): across the back - the rocket launcher.
The two temples are point-mirrored (turned half round), so each side's left is the same.  Islands stand in an abyss whose
floor shows the sky: fall in and you die (game.c).

usage: mkfacing.py out.cgz        (writes out.cfg beside it)
"""
import sys, os, struct, gzip, math
import numpy as np

N = 128
SOLID, CORNER, FHF, CHF, SPACE = 0, 1, 2, 3, 4
# entity types (Cube) + ours
LIGHT, PLAYERSTART, SHELLS, BULLETS, ROCKETS, ROUNDS, HEALTH, BOOST, GREENARMOUR, YELLOWARMOUR, QUAD, TELEPORT, TELEDEST = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
FLAG = 18                     # ours: attr2 = team (0 red, 1 blue); Cube ignores it

# texture slots (this map's own list, written to the .cfg; 0 = sky)
TEX = ['ik2k/ik_sky_day_back.jpg',            # 0 sky (dummy, as Cube's list)
       'golgotha/water2.jpg',                 # 1 liquid (dummy)
       'egyptsoc/stone02b.jpg',               # 2 temple brick
       'egyptsoc/block07b.jpg',               # 3 temple floor
       'egyptsoc/128-03c.jpg',                # 4 hall ceiling: gold meander
       'egyptsoc/v064-03b.jpg',               # 5 pillar: hieroglyph columns
       'egyptsoc/128-01pc.jpg',               # 6 island sand
       'egyptsoc/stone03d.jpg',               # 7 cliff rock
       'egyptsoc/block01d.jpg',               # 8 bridge
       'egyptsoc/lig_b064-02d.jpg',           # 9 red team band
       'egyptsoc/lig_b064-02a.jpg',           # 10 blue team band
       'egyptsoc/lig_064-02r1.jpg',           # 11 red disc (flag wall)
       'egyptsoc/lig_064-04b1.jpg',           # 12 blue disc
       'egyptsoc/064-07d.jpg',                # 13 trim
       'egyptsoc/s064-05c.jpg',               # 14 eye of Horus (teleporter walls)
       'egyptsoc/stone10b.jpg',               # 15 level floors
       ]
SKY, BRICK, TFLOOR, HCEIL, PILLAR, SAND, ROCK, BRIDGE, REDBAND, BLUEBAND, REDDISC, BLUEDISC, TRIM, EYE, LFLOOR = 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15

SKYH = 80          # sky "ceiling" everywhere outdoors
ABYSS = -126       # the abyss floor (shows the sky)

T = np.full((N, N), SPACE, np.int32); F = np.full((N, N), ABYSS, np.int32); C = np.full((N, N), SKYH, np.int32)
W = np.full((N, N), ROCK, np.int32); FT = np.full((N, N), SKY, np.int32); CT = np.full((N, N), SKY, np.int32); U = np.full((N, N), ROCK, np.int32)
ents = []

def ent(x, y, z, t, a1=0, a2=0, a3=0, a4=0): ents.append((int(x), int(y), int(z), int(a1), t, a2, a3, a4))
def cell(x, y, t=SPACE, f=None, c=None, w=None, ft=None, ct=None, u=None):
    if not (0 <= x < N and 0 <= y < N): return
    T[y, x] = t
    if f is not None: F[y, x] = f
    if c is not None: C[y, x] = c
    if w is not None: W[y, x] = w; U[y, x] = w if u is None else u
    if ft is not None: FT[y, x] = ft
    if ct is not None: CT[y, x] = ct
def ground(x, y, ft, w=ROCK, f=0): cell(x, y, SPACE, f, SKYH, w, ft, SKY)

# ---- islands: rough ovals of sand, and the bridge with a platform half way ----
def island(cx, cy, rx, ry, seed):
    rnd = np.random.RandomState(seed)
    k = rnd.uniform(0.85, 1.12, 12)
    for y in range(N):
        for x in range(N):
            a = math.atan2(y - cy, x - cx); i = (a + math.pi) / (2 * math.pi) * 12
            r = k[int(i) % 12] * (1 - (i % 1)) + k[(int(i) + 1) % 12] * (i % 1)
            if ((x - cx) / (rx * r)) ** 2 + ((y - cy) / (ry * r)) ** 2 <= 1: ground(x, y, SAND)
island(20, 64, 19, 32, 1)
for x in range(3, 42):                                           # a walkway at least 5 wide all round the temple
    for y in range(42, 86): ground(x, y, SAND)
MID = 12                                                         # the middle platform's height: uphill all the way to it
def arm(pts, width=5):
    """a bridge arm along a cubic curve, climbing from 0 at the island to MID at the middle (one unit at a time)"""
    (ax, ay), (bx, by), (cx, cy), (dx, dy) = pts
    for k in range(1200):
        t = k / 1199; u = 1 - t
        x = u ** 3 * ax + 3 * u * u * t * bx + 3 * u * t * t * cx + t ** 3 * dx
        y = u ** 3 * ay + 3 * u * u * t * by + 3 * u * t * t * cy + t ** 3 * dy
        h = int(round(MID * t))
        for ddx in range(-width // 2, width - width // 2):
            for ddy in range(-width // 2, width - width // 2):
                xx, yy = int(round(x)) + ddx, int(round(y)) + ddy
                if 0 <= xx < N and 0 <= yy < N and (F[yy, xx] < h or FT[yy, xx] != BRIDGE): ground(xx, yy, BRIDGE, f=h)
# two arms from each temple, swinging wide north and south before turning in to the middle: a long run in the open
for pts in (((42, 47), (47, 4), (53, 8), (58, 52)), ((42, 80), (47, 123), (53, 119), (58, 75))):
    arm(pts)
    arm(tuple((N - x, N - y) for (x, y) in pts))
for x in range(56, 72):                                          # the platform in the middle, up top
    for y in range(52, 76): ground(x, y, BRIDGE, f=MID)
for (x, y) in ((59, 56), (67, 56), (59, 70), (67, 70), (63, 63)):   # its cover blocks (jump on them)
    for dx in range(2):
        for dy in range(2): cell(x + dx, y + dy, SPACE, MID + 3, SKYH, BRICK, TFLOOR, SKY)

# ---- the red temple, in its own frame: d = depth from the front face (0..27), w = across (0..31); x = 41 - d, y = 48 + w ----
def T_xy(d, w): return 36 - d, 48 + w
def tcell(dd, ww, **k): x, y = T_xy(dd, ww); cell(x, y, **k)
def temple(team):
    band = REDBAND if team == 0 else BLUEBAND; disc = REDDISC if team == 0 else BLUEDISC
    for d in range(28):
        for w in range(32): tcell(d, w, t=SOLID, w=BRICK)
    for d in range(0, 2):                                        # the front: team bands either side of the door
        for w in list(range(6, 14)) + list(range(18, 26)): tcell(d, w, t=SOLID, w=band)
    # hall: d 2..21, w 6..25, ceiling 14; door w 14..17 through the front wall
    for d in range(2, 22):
        for w in range(6, 26): tcell(d, w, t=SPACE, f=0, c=14, w=BRICK, ft=TFLOOR, ct=HCEIL, u=TRIM)
    for d in range(0, 2):
        for w in range(14, 18): tcell(d, w, t=SPACE, f=0, c=10, w=BRICK, ft=TFLOOR, ct=TRIM, u=band)
    # the walls by the flag and by the teleporters carry the team disc / the eye
    for d in range(6, 13): tcell(d, 26, t=SOLID, w=disc)
    for d in (4, 5, 6, 16, 17, 18): tcell(d, 5, t=SOLID, w=EYE)
    # the pillar: 8x8 (d 6..13, w 12..19), a 3-wide way in from its back (d 9..13, w 15..17) to the teleporter
    # (a body is 2.2 cells across: anything narrower than 3 cells cannot be walked through)
    for d in range(6, 14):
        for w in range(12, 20): tcell(d, w, t=SOLID, w=PILLAR)
    for d in range(9, 14):
        for w in range(15, 18): tcell(d, w, t=SPACE, f=0, c=12, w=PILLAR, ft=TFLOOR, ct=HCEIL, u=PILLAR)
    # roof (48): the whole left flank's front 18 cells; level 3 (32): the right flank's; level 2 (16): the back, all across
    for side in (range(0, 5), range(27, 32)):
        for w in side:
            for d in range(0, 18): tcell(d, w, t=SPACE, f=48 if side.start == 0 else 32, c=SKYH, w=BRICK, ft=LFLOOR, ct=SKY, u=TRIM)
            for d in range(18, 28): tcell(d, w, t=SPACE, f=16, c=SKYH, w=BRICK, ft=LFLOOR, ct=SKY, u=TRIM)
    for d in range(23, 28):
        for w in range(5, 27): tcell(d, w, t=SPACE, f=16, c=SKYH, w=BRICK, ft=LFLOOR, ct=SKY, u=TRIM)
    for d in (0, 1):                                             # a step of parapet at the very front of each flank
        for w in range(0, 5): tcell(d, w, t=SPACE, f=49, c=SKYH, w=band, ft=TRIM, ct=SKY, u=band)
        for w in range(27, 32): tcell(d, w, t=SPACE, f=33, c=SKYH, w=band, ft=TRIM, ct=SKY, u=band)
    for (d, w) in ((0, 0), (0, 30)):                             # two spires at the front corners (to the sky)
        for dd in range(2):
            for ww in range(2): tcell(d + dd, w + ww, t=SOLID, w=band)

def P(d, w):
    """lattice point in the red temple's frame -> map x, y"""
    return 36 - d, 48 + w

temple(0)
# entities of the red temple (team 0), in its frame; the blue ones are the same turned round
def temple_ents(team):
    def e(d, w, z, t, a1=0, a2=0, a3=0, a4=0):
        x, y = P(d, w)
        if team: x, y = N - 1 - x, N - 1 - y          # the same turn as the walls below (cell centres)
        if t in (TELEDEST, PLAYERSTART): a1 = (a1 + (180 if team else 0)) % 360
        ents.append((x, y, z, a1, t, a2, a3, a4))
    tag = 1 + team * 6
    # up: from the hall - near the door to level 2, at the back to level 3, in the pillar to the roof
    e(5, 8, 0, TELEPORT, tag); e(17, 8, 0, TELEPORT, tag + 1); e(10, 16, 0, TELEPORT, tag + 2)
    e(25, 16, 16, TELEDEST, 270, tag); e(3, 29, 32, TELEDEST, 270, tag + 1); e(3, 2, 48, TELEDEST, 270, tag + 2)
    # down: from the far end of each, out in front of the door (left, middle, right)
    e(16, 2, 48, TELEPORT, tag + 3); e(-3, 6, 0, TELEDEST, 270, tag + 3)          # roof
    e(16, 29, 32, TELEPORT, tag + 4); e(-3, 25, 0, TELEDEST, 270, tag + 4)        # level 3
    e(25, 2, 16, TELEPORT, tag + 5); e(-3, 16, 0, TELEDEST, 270, tag + 5)         # level 2
    e(25, 12, 16, ROCKETS); e(25, 20, 16, ROCKETS); e(25, 24, 16, HEALTH)                            # level 2: rockets
    e(7, 29, 32, BULLETS); e(9, 29, 32, BULLETS); e(11, 29, 32, GREENARMOUR)                          # level 3: chaingun
    e(7, 2, 48, ROUNDS); e(9, 2, 48, ROUNDS); e(11, 2, 48, ROUNDS)                                    # roof: the rifle and its ammo
    e(9, 23, 0, FLAG, 0, team)                                                                       # flag, right of the door
    e(12, -3, 0, SHELLS); e(12, 34, 0, SHELLS)                                                       # the shotgun: along the sides
    e(19, 22, 0, HEALTH); e(21, 15, 0, HEALTH); e(3, 22, 0, HEALTH)
    for (d, w, a) in ((19, 20, 270), (20, 13, 270), (15, 23, 270), (17, 21, 270), (20, 17, 270)):     # (all well out of the teleporters' 4 units)
        e(d, w, 0, PLAYERSTART, a, team)
    # lights: warm in the hall, the team colour on the roofs and the door
    rgb = (255, 90, 60) if team == 0 else (70, 110, 255)
    e(12, 16, 8, LIGHT, 16, 150, 130, 100); e(4, 9, 8, LIGHT, 10, 140, 120, 100); e(18, 9, 8, LIGHT, 10, 140, 120, 100); e(9, 23, 8, LIGHT, 10, *rgb)
    e(3, 2, 52, LIGHT, 14, *rgb); e(3, 29, 36, LIGHT, 14, *rgb); e(-1, 16, 6, LIGHT, 12, *rgb); e(13, 2, 52, LIGHT, 14, 170, 150, 120)
    e(13, 29, 36, LIGHT, 12, 170, 150, 120); e(25, 16, 20, LIGHT, 16, 170, 150, 120); e(12, -3, 6, LIGHT, 12, 160, 150, 130); e(12, 34, 6, LIGHT, 12, 160, 150, 130)
temple_ents(0)
ents.append((64, 64, MID, 8, 20, 12, 0, 0))                     # King of the Hill: the middle platform (56-71 x 52-75)

# the blue temple: the red one turned half round about the map's middle
for y in range(N):
    for x in range(N):
        sx, sy = N - 1 - x, N - 1 - y
        if 9 <= sx <= 36 and 48 <= sy <= 79:
            T[y, x], F[y, x], C[y, x], W[y, x], FT[y, x], CT[y, x], U[y, x] = T[sy, sx], F[sy, sx], C[sy, sx], W[sy, sx], FT[sy, sx], CT[sy, sx], U[sy, sx]
            if W[y, x] == REDBAND: W[y, x] = BLUEBAND
            if U[y, x] == REDBAND: U[y, x] = BLUEBAND
            if W[y, x] == REDDISC: W[y, x] = BLUEDISC
# the blue island is the red one turned round too (the bridge is already symmetric enough)
for y in range(N):
    for x in range(N):
        sx, sy = N - 1 - x, N - 1 - y
        if x >= 80 and T[sy, sx] != SOLID and F[sy, sx] == 0 and FT[sy, sx] == SAND and T[y, x] == SPACE and F[y, x] == ABYSS:
            ground(x, y, SAND)
temple_ents(1)

# daylight over the islands and the bridge: big soft lights in a grid (Cube has no ambient light)
for x in range(6, 124, 13):
    for y in range(10, 120, 13):
        if T[y, x] == SPACE and F[y, x] >= 0 and FT[y, x] in (SAND, BRIDGE): ent(x, y, F[y, x] + 6, LIGHT, 26, 120, 115, 110)

# the map's edge: two cells of solid all round (Cube needs them)
T[:2, :] = SOLID; T[-2:, :] = SOLID; T[:, :2] = SOLID; T[:, -2:] = SOLID
# ...faced with the sky, so it is not a black wall all round the horizon: a wall of texture 0 shows the sky box
W[:2, :] = SKY; W[-2:, :] = SKY; W[:, :2] = SKY; W[:, -2:] = SKY
# the abyss cells' own wall faces too: seen from far off they would otherwise be drawn as black (fogged) walls in front
# of the sky box; the islands' cliffs keep their rock (they take the island cells' texture)
ab = (T == SPACE) & (F == ABYSS); W[ab] = SKY; U[ab] = SKY

def write(path):
    title = b'Facing Cubes - after Facing Worlds (UT99)'
    hdr = struct.pack('<4siiii', b'CUBE', 5, 20 + 128 + 768 + 4 + 60, 7, len(ents))
    hdr += title.ljust(128, b'\0') + bytes(range(256)) * 3 + struct.pack('<i', -100000) + b'\0' * 60
    body = bytearray()
    for e in ents: body += struct.pack('<hhhhBBBB', *e)
    for y in range(N):
        for x in range(N):
            t = int(T[y, x])
            if t == SOLID: body += bytes([SOLID, int(W[y, x]), 0])
            else: body += bytes([t, F[y, x] & 255, C[y, x] & 255, int(W[y, x]), int(FT[y, x]), int(CT[y, x]), 0, int(U[y, x]), 0])
    with gzip.open(path, 'wb') as f: f.write(hdr + bytes(body))
    with open(os.path.splitext(path)[0] + '.cfg', 'w') as f:
        f.write('// Facing Cubes: a Cube take on Unreal Tournament\'s Facing Worlds (made by tools/mkfacing.py)\ntexturereset\n')
        for t in TEX: f.write('texture 0 "%s"\n' % t)
    print('%s: %d entities, %d open cells' % (path, len(ents), int((T != SOLID).sum())))

write(sys.argv[1])
