#!/usr/bin/env python3
"""mkxeno.py - "XENO", the Cube PSX map pack by Xenocide21: an 8-map single-player campaign and 3 multiplayer maps.
Each is built by hand on tools/xmap.py's grid, played through by its checker, and written as a Cube map (.cgz + .cfg)
for cgz2cmp.py / mktex.py.  Tuned for the PlayStation: closed rooms of moderate size, corridors 3-4 wide, a few dozen
monsters, the odd courtyard open to the sky (the star sky on the night maps, which are lit darker).

usage: mkxeno.py outdir [name ...]      (writes outdir/<name>.cgz/.cfg and outdir/<name>.png previews; checks each)
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xmap import *

AUTHOR = 'Xenocide21'
DIR = 'xeno'

# ---- texture themes: 0 sky, 1 water (Cube's liquid slot), then the theme's own ----
TECH = ['ik2k/ik_sky_day_back.jpg', 'golgotha/water2.jpg',
        'tech1soc/mix_wall03a.jpg', 'ikbase/ik_floor_met128e.jpg', 'tech1soc/panel20c.jpg', 'tech1soc/grate1b.jpg',
        'ikbase/ik_cwall04a.jpg', 'tech1soc/met_wall01a.jpg', 'ikbase/ik_gtrimh_lit1a.jpg', 'tech1soc/064lig22bb.jpg',
        'ikbase/ik_floor_conc128a.jpg', 'tech1soc/block01b.jpg', 'tech1soc/032lig12ya.jpg', 'ikbase/ik_metplatef_6464b.jpg']
T_WALL, T_FLOOR, T_CEIL, T_GRATE, T_CONC, T_DOOR, T_TRIM, T_LAMP, T_CFLOOR, T_BLOCK, T_WARN, T_PLATE = range(2, 14)
STONE = ['ik2k/ik_sky_day_back.jpg', 'golgotha/water2.jpg',
         'ik2k/ik_brick_6464a.jpg', 'ik2k/ik_ground_cobbles128.jpg', 'ik2k/ik_wood_planks128b.jpg', 'ik2k/ik_ground_grass128.jpg',
         'ik2k/ik_brickwall_gry.jpg', 'ik2k/ik_woodwall128128b.jpg', 'ik2k/ik_floor_tile128b.jpg', 'ik2k/ik_cliff256.jpg',
         'ik2k/ik_stonestep25632.jpg', 'ik2k/ik_floor_wood128d.jpg', 'ik2k/ik_brick_12864a.jpg', 'ik2k/ik_ground_pebbles128.jpg']
S_BRICK, S_COBBLE, S_PLANK, S_GRASS, S_GREY, S_WOOD, S_TILE, S_ROCK, S_STEP, S_WFLOOR, S_BRICK2, S_PEBBLE = range(2, 14)
EGYPT = ['ik2k/ik_sky_day_back.jpg', 'golgotha/water2.jpg',
         'egyptsoc/stone02b.jpg', 'egyptsoc/block07b.jpg', 'egyptsoc/128-03c.jpg', 'egyptsoc/v064-02a.jpg',
         'egyptsoc/128-01pc.jpg', 'egyptsoc/stone03d.jpg', 'egyptsoc/block01d.jpg', 'egyptsoc/lig_064-04y1.jpg',
         'egyptsoc/s064-05c.jpg', 'egyptsoc/stone10b.jpg', 'egyptsoc/064-07d.jpg', 'egyptsoc/solidgrate1b.jpg']
E_STONE, E_FLOOR, E_CEIL, E_PILLAR, E_SAND, E_ROCK, E_BLOCK, E_GOLD, E_EYE, E_SLAB, E_TRIM, E_GRATE = range(2, 14)

WARM = (255, 230, 190); COOL = (170, 200, 255); DIMW = (150, 140, 120); NIGHT = (90, 110, 170); RED = (255, 90, 60); GREEN = (140, 255, 150)
EXIT = 50                                                       # the exit carrot's tag on every map

# ======================================================================================================================
def xeno01():
    """INTAKE - a pumping station.  The pump hall's carrot opens the way on - and the closet behind you.  Crates hide the
    store's carrot; the control room's sits on a platform only a jump pad reaches, and it opens the exit."""
    m = Map(128, 'Intake - by %s' % AUTHOR, TECH, water=-1)
    m.room(10, 10, 26, 26, 0, 12, T_WALL, T_FLOOR, T_CEIL)                    # A: the airlock
    m.start(13, 18, 270); m.lights(10, 10, 26, 26, COOL, 8)
    m.item(22, 12, HEALTH); m.item(22, 23, CLIPS)
    m.corridor(26, 16, 40, 16, 4, 0, 10, T_CONC, T_GRATE, T_CEIL)            # to the pump hall
    m.light(33, 18, 8, COOL)
    m.room(40, 6, 66, 36, 0, 14, T_WALL, T_CFLOOR, T_CEIL)                    # B: the pump hall
    m.pool(40, 18, 66, 24, -6, T_GRATE)                                       # the channel across it
    m.floor(51, 18, 55, 24, 0, T_PLATE)                                       # the one bridge
    m.solid(44, 9, 47, 13, T_BLOCK); m.solid(58, 9, 61, 13, T_BLOCK); m.solid(44, 28, 47, 32, T_BLOCK)     # pumps
    m.lights(40, 6, 66, 36, COOL, 9)
    m.mon(60, 28, OGRO, 90); m.mon(46, 23, GOBLIN, 270); m.mon(62, 10, OGRO, 180)
    m.carrot(62, 32, 1)                                                       # opens the store - and the closet
    m.item(42, 33, SHELLS); m.item(64, 7, HEALTH)
    m.room(44, 37, 54, 44, 0, 10, T_BLOCK, T_CFLOOR, T_CEIL)                  # the closet: behind the start's back
    m.door(46, 36, 51, 37, 1, 0, 10, T_DOOR)
    m.mon(47, 41, RHINO, 0); m.mon(51, 41, RHINO, 0); m.light(49, 40, 6, RED)
    m.door(66, 12, 67, 16, 1, 0, 12, T_DOOR)                                  # door 1: to the store
    m.corridor(67, 12, 72, 12, 4, 0, 12, T_CONC, T_GRATE, T_CEIL)
    m.room(72, 4, 96, 30, 0, 14, T_BLOCK, T_CFLOOR, T_CEIL)                   # C: the store, crates in rows
    for cx in (76, 82, 88):
        m.solid(cx, 8, cx + 3, 12, T_PLATE); m.solid(cx, 17, cx + 3, 21, T_PLATE)
    m.solid(92, 22, 96, 30, T_PLATE)                                          # the stack in the corner...
    m.room(89, 25, 92, 30, 0, 14)                                             # ...with a gap behind it
    m.carrot(94, 27, 2)                                                       # (the room's last corner: round the stack)
    m.room(92, 26, 96, 29, 0, 14)
    m.lights(72, 4, 96, 30, WARM, 10)
    m.mon(80, 14, RATA, 180); m.mon(90, 6, GOBLIN, 90); m.mon(86, 25, OGRO, 0)
    m.item(74, 6, BULLETS); m.item(74, 28, GREENARMOUR)
    m.door(80, 30, 84, 31, 2, 0, 12, T_DOOR)                                  # door 2: down to control
    m.corridor(80, 31, 80, 40, 4, 0, 12, T_CONC, T_GRATE, T_CEIL)
    m.room(70, 40, 98, 64, 0, 16, T_WALL, T_FLOOR, T_CEIL)                    # D: the control room
    m.floor(86, 42, 96, 49, 5, T_PLATE)                                       # the platform (too high to jump)
    m.pad(82, 45, 26)                                                         # the pad beside it
    m.carrot(92, 45, 3)
    m.pool(72, 55, 84, 63, -5, T_GRATE)                                       # a sump in the corner
    m.item(78, 59, ROCKETS)
    m.lights(70, 40, 98, 64, COOL, 9); m.light(91, 45, 8, GREEN)
    m.mon(76, 46, SLITH, 270); m.mon(92, 57, RHINO, 90); m.mon(94, 52, GOBLIN, 180)
    m.room(86, 66, 95, 74, 0, 10, T_BLOCK, T_CFLOOR, T_CEIL)                  # its closet (door 3 too)
    m.door(88, 64, 93, 66, 3, 0, 10, T_DOOR)
    m.mon(88, 71, OGRO, 0); m.mon(92, 71, OGRO, 0); m.light(90, 70, 6, RED)
    m.door(98, 54, 99, 59, 3, 0, 12, T_DOOR)                                  # door 3: the exit
    m.corridor(99, 54, 106, 54, 5, 0, 12, T_CONC, T_GRATE, T_CEIL)
    m.room(106, 44, 120, 60, 0, 12, T_WALL, T_FLOOR, T_CEIL)                  # E: the outflow - the exit
    m.carrot(116, 52, EXIT, 'xeno02'); m.light(113, 52, 10, GREEN); m.item(108, 46, HEALTH)
    return m

# ======================================================================================================================
def xeno02():
    """FLOODED WORKS - the factory hall is under water but for its walkways and two islands.  The north gate's carrot is
    in a sunken chamber only an underwater tunnel reaches; taking it lets the hellpigs out of the south pens."""
    m = Map(128, 'Flooded Works - by %s' % AUTHOR, TECH, water=0)
    m.room(8, 50, 24, 68, 1, 12, T_WALL, T_FLOOR, T_CEIL); m.start(11, 59, 270)      # start: the gantry office
    m.lights(8, 50, 24, 68, COOL, 8); m.item(20, 52, SHELLS); m.item(20, 65, CLIPS)
    m.corridor(24, 56, 32, 56, 5, 1, 12, T_CONC, T_GRATE, T_CEIL)
    m.room(32, 30, 82, 90, -6, 13, T_WALL, T_GRATE, T_CEIL)                          # the hall, flooded
    m.floor(32, 30, 82, 35, 1, T_PLATE); m.floor(32, 85, 82, 90, 1, T_PLATE)          # walkways north and south
    m.floor(32, 30, 37, 90, 1, T_PLATE)                                              # and west
    m.floor(46, 50, 54, 64, 1, T_PLATE); m.floor(62, 62, 72, 72, 1, T_PLATE)          # two islands
    for px in (42, 58, 74):
        for py in (42, 76): m.solid(px, py, px + 2, py + 2, T_BLOCK)                 # pillars in the water
    m.wall(56, 30, 58, 90, T_WALL, gaps=((30, 35), (66, 76), (85, 90)))                 # arcades: four bays
    m.wall(32, 58, 82, 60, T_WALL, gaps=((32, 37), (44, 56), (64, 78)))
    m.lights(32, 30, 82, 90, COOL, 10)
    m.mon(50, 56, SLITH, 270); m.mon(66, 66, GOBLIN, 90); m.mon(60, 32, OGRO, 0); m.mon(76, 87, OGRO, 180); m.mon(34, 80, GOBLIN, 270)
    m.item(50, 52, BULLETS); m.item(68, 70, HEALTH); m.item(78, 32, ROCKETS)
    m.room(82, 70, 98, 75, -9, -2, T_CONC, T_GRATE, T_CONC)                          # the tunnel, all under water
    m.room(98, 62, 112, 82, -6, 10, T_BLOCK, T_GRATE, T_CEIL)                         # the sunken chamber...
    m.floor(102, 64, 112, 72, 1, T_PLATE)                                            # ...and its dry ledge
    m.carrot(108, 67, 1); m.light(106, 68, 8, GREEN); m.item(110, 78, GREENARMOUR)
    m.mon(104, 76, HELLPIG, 270)
    for px in (40, 56, 70):                                                          # the pens on the south walkway
        m.room(px, 91, px + 8, 98, 1, 10, T_BLOCK, T_CFLOOR, T_CEIL); m.door(px + 2, 90, px + 6, 91, 1, 1, 10, T_DOOR)
        m.mon(px + 4, 95, HELLPIG, 0); m.light(px + 4, 94, 5, RED)
    m.door(54, 29, 60, 30, 1, 1, 12, T_DOOR)                                         # door 1: the north gate
    m.corridor(54, 22, 54, 22, 6, 1, 12, T_CONC, T_GRATE, T_CEIL); m.room(54, 22, 60, 29, 1, 12)
    m.room(40, 6, 76, 22, 1, 14, T_WALL, T_FLOOR, T_CEIL)                            # the control deck
    m.floor(66, 8, 74, 14, 4, T_PLATE)                                               # its desk, up a step
    m.carrot(70, 10, 2); m.lights(40, 6, 76, 22, WARM, 9)
    m.mon(46, 10, RHINO, 270); m.mon(60, 18, KNIGHT, 180); m.mon(72, 18, RHINO, 90)
    m.item(42, 20, HEALTH); m.item(50, 8, SHELLS)
    m.door(76, 16, 77, 21, 2, 1, 12, T_DOOR)                                         # door 2: out
    m.corridor(77, 16, 86, 16, 5, 1, 12, T_CONC, T_GRATE, T_CEIL)
    m.room(86, 8, 104, 26, 1, 12, T_WALL, T_FLOOR, T_CEIL)                            # the outflow: the exit
    m.carrot(100, 17, EXIT, 'xeno03'); m.light(96, 17, 10, GREEN); m.mon(94, 12, GOBLIN, 90); m.mon(94, 22, GOBLIN, 90)
    return m

def xeno03():
    """NIGHT COURTYARD - castle grounds under the stars.  Three towers each keep a carrot (one up its stair, one up a jump
    pad, one in a guarded loft whose taking opens the ogres' stable); the keep's gate has three locks, one for each."""
    m = Map(256, 'Night Courtyard - by %s' % AUTHOR, STONE, water=-2, ambient=(26, 30, 46))
    m.room(64, 64, 168, 168, 0, sky=True, w=S_BRICK, ft=S_GRASS)                       # the courtyard
    m.pool(104, 104, 128, 128, -7, S_PEBBLE); m.floor(112, 112, 120, 120, 0, S_TILE)  # the fountain, its plinth
    m.floor(64, 64, 168, 70, 0, S_COBBLE); m.floor(64, 162, 168, 168, 0, S_COBBLE)
    for (x, y) in ((84, 84), (144, 84), (84, 144), (144, 144)): m.solid(x, y, x + 3, y + 3, S_GREY)   # statues
    for c in (98, 132):                                                               # hedge walls: nine courts round the fountain
        m.wall(c, 64, c + 2, 168, S_ROCK, gaps=((78, 90), (110, 122), (142, 154)))
        m.wall(64, c, 168, c + 2, S_ROCK, gaps=((78, 90), (110, 122), (142, 154)))
    for (x, y) in ((80, 80), (150, 80), (80, 150), (150, 150), (116, 98), (98, 116), (134, 116), (116, 134)): m.light(x, y, 20, NIGHT)
    for (x, y, k, a) in ((90, 100, OGRO, 90), (140, 100, GOBLIN, 270), (100, 140, OGRO, 0), (130, 150, GOBLIN, 180),
                         (116, 90, KNIGHT, 0), (110, 137, RATA, 180), (150, 120, OGRO, 90), (76, 120, GOBLIN, 270)): m.mon(x, y, k, a)
    m.item(116, 116, HEALTH); m.item(100, 70, SHELLS); m.item(132, 165, BULLETS)
    # the towers, one at each corner: SW is where you come in
    def tower(x0, y0, ft=S_WFLOOR):
        m.room(x0, y0, x0 + 24, y0 + 24, 0, 16, S_GREY, ft, S_PLANK); m.lights(x0, y0, x0 + 24, y0 + 24, WARM, 12)
    tower(36, 172); m.start(46, 186, 180); m.item(42, 176, CLIPS); m.item(54, 176, GREENARMOUR)
    m.room(54, 160, 70, 178, 0, 12, S_GREY, S_COBBLE, S_PLANK)                        # SW tower -> yard
    tower(36, 36)                                                                     # NW: a stair up to its carrot
    m.stairs(40, 40, 56, 44, 0, 8, 'x', S_STEP); m.floor(52, 40, 58, 50, 8, S_WFLOOR)
    m.carrot(55, 46, 1); m.mon(44, 52, OGRO, 0); m.mon(54, 56, RATA, 0)
    m.room(54, 54, 70, 70, 0, 12, S_GREY, S_COBBLE, S_PLANK)                          # NW tower -> yard
    tower(172, 36)                                                                    # NE: a jump pad to its loft
    m.floor(184, 38, 194, 48, 9, S_WFLOOR); m.pad(180, 52, 30); m.carrot(189, 42, 2)
    m.mon(178, 56, GOBLIN, 90); m.mon(190, 54, KNIGHT, 90)
    m.room(162, 54, 178, 70, 0, 12, S_GREY, S_COBBLE, S_PLANK)                        # NE tower -> yard
    tower(172, 172)                                                                   # SE: the guarded loft
    m.floor(184, 184, 194, 194, 4, S_WFLOOR); m.carrot(189, 189, 3)
    m.mon(180, 180, KNIGHT, 180); m.mon(190, 176, RHINO, 180)
    m.room(162, 160, 178, 178, 0, 12, S_GREY, S_COBBLE, S_PLANK)                      # SE tower -> yard
    m.room(176, 140, 196, 160, 0, 12, S_WOOD, S_WFLOOR, S_PLANK)                      # the stable it opens
    m.door(182, 160, 188, 172, 3, 0, 12, S_WOOD)
    for x in (180, 186, 192): m.mon(x, 146, OGRO, 0)
    m.light(186, 150, 8, RED)
    # the keep: three gates, one behind the other, a lock each
    m.room(104, 30, 128, 56, 0, 16, S_BRICK2, S_TILE, S_PLANK); m.lights(104, 30, 128, 56, WARM, 10)
    m.door(110, 56, 122, 58, 1, 0, 14, S_WOOD); m.door(110, 58, 122, 60, 2, 0, 14, S_WOOD); m.door(110, 60, 122, 64, 3, 0, 14, S_WOOD)
    m.carrot(116, 36, EXIT, 'xeno04'); m.mon(108, 40, KNIGHT, 0); m.mon(124, 40, KNIGHT, 0); m.mon(116, 48, BAUUL, 0)
    m.item(106, 52, ROCKETS); m.item(126, 52, HEALTH)
    return m

def xeno04():
    """THE VAULTS - a tomb.  The sanctum door has four seals; each side vault keeps one: an island in a flooded vault,
    a hidden room behind a teleporter, the top of a stepped pyramid, and a sarcophagus whose carrot wakes its guards."""
    m = Map(128, 'The Vaults - by %s' % AUTHOR, EGYPT, water=-3)
    m.room(58, 90, 70, 122, 0, 12, E_STONE, E_FLOOR, E_CEIL); m.start(64, 118, 180)   # the way in
    m.lights(58, 96, 70, 122, WARM, 9); m.item(60, 100, CLIPS)
    m.room(42, 50, 86, 92, 0, 14, E_STONE, E_SLAB, E_CEIL)                          # the great chamber
    for (x, y) in ((50, 58), (76, 58), (50, 82), (76, 82)): m.solid(x, y, x + 3, y + 3, E_PILLAR)
    m.lights(42, 50, 86, 92, WARM, 11)
    m.mon(54, 70, OGRO, 90); m.mon(74, 70, OGRO, 270); m.mon(64, 60, GOBLIN, 0)
    # W: the flooded vault, its seal on an island
    m.corridor(30, 66, 42, 66, 5, 0, 12, E_STONE, E_FLOOR, E_CEIL)
    m.room(8, 54, 30, 80, -8, 14, E_ROCK, E_GRATE, E_CEIL); m.floor(26, 54, 30, 80, 0, E_FLOOR)
    m.floor(12, 62, 19, 71, -1, E_SLAB); m.carrot(15, 66, 1); m.mon(15, 75, SLITH, 0); m.light(15, 66, 10, COOL)
    # E: the teleporter vault - the pad on the dais takes you to the hidden room
    m.corridor(86, 66, 96, 66, 5, 0, 12, E_STONE, E_FLOOR, E_CEIL)
    m.room(96, 54, 120, 80, 0, 14, E_STONE, E_FLOOR, E_CEIL); m.lights(96, 54, 120, 80, WARM, 12)
    m.tele(112, 66, 7); m.mon(104, 58, GOBLIN, 270); m.mon(104, 76, GOBLIN, 270)
    m.room(100, 6, 118, 20, 0, 10, E_EYE, E_SLAB, E_CEIL); m.dest(103, 13, 7, 270)   # the hidden room
    m.carrot(114, 13, 2); m.tele(109, 9, 8); m.dest(100, 70, 8, 90); m.light(109, 13, 8, GREEN)
    # SW: the pyramid
    m.room(30, 80, 47, 86, 0, 12, E_STONE, E_FLOOR, E_CEIL); m.room(30, 80, 36, 96, 0, 12)
    m.room(10, 94, 42, 120, 0, 14, E_STONE, E_SAND, E_CEIL)
    for k in range(5): m.floor(14 + 2 * k, 98 + 2 * k, 34 - 2 * k, 118 - 2 * k, k + 1, E_BLOCK)
    m.carrot(24, 108, 3); m.lights(10, 94, 42, 120, WARM, 12); m.mon(14, 116, RATA, 0); m.mon(38, 98, OGRO, 180)
    # SE: the sarcophagus - its carrot opens the guards' alcoves
    m.room(81, 80, 98, 86, 0, 12, E_STONE, E_FLOOR, E_CEIL); m.room(92, 80, 98, 96, 0, 12)
    m.room(86, 94, 118, 120, 0, 14, E_STONE, E_FLOOR, E_CEIL); m.lights(86, 94, 118, 120, WARM, 12)
    m.solid(98, 104, 106, 110, E_BLOCK); m.carrot(102, 112, 4)
    for x in (90, 112):
        m.room(x - 3, 121, x + 4, 126, 0, 10, E_STONE, E_FLOOR, E_CEIL); m.door(x - 2, 120, x + 3, 121, 4, 0, 10, E_EYE)
        m.mon(x, 123, KNIGHT, 180); m.mon(x + 2, 124, GOBLIN, 180)
    # N: the sanctum, behind its four seals
    for k in range(4): m.door(56, 49 - k, 72, 50 - k, k + 1, 0, 14, E_EYE)
    m.room(56, 43, 72, 46, 0, 14)
    m.room(52, 22, 76, 43, 0, 16, E_GOLD, E_SLAB, E_CEIL); m.lights(52, 22, 76, 43, WARM, 9)
    m.carrot(64, 26, EXIT, 'xeno05'); m.mon(56, 30, BAUUL, 0); m.item(72, 38, HEALTH); m.item(56, 38, ROCKETS)
    return m

def xeno05():
    """CISTERNS - the old cisterns under the city.  A forest of pillars in each flooded hall; the sluice opens from the
    first hall's island, the second hall's key is down a drowned passage, and the third is crossed on jump pads."""
    m = Map(256, 'Cisterns - by %s' % AUTHOR, STONE, water=0)
    m.room(6, 40, 22, 58, 1, 12, S_BRICK, S_TILE, S_BRICK2); m.start(10, 49, 270); m.lights(6, 40, 22, 58, WARM, 8)
    m.item(18, 42, CLIPS); m.item(18, 55, SHELLS)
    m.corridor(22, 46, 30, 46, 5, 1, 12, S_BRICK, S_TILE, S_BRICK2)
    def hall(x0, y0, x1, y1):
        m.room(x0, y0, x1, y1, -6, 13, S_BRICK, S_PEBBLE, S_BRICK2)
        m.floor(x0, y0, x0 + 5, y1, 1, S_TILE); m.floor(x0, y0, x1, y0 + 5, 1, S_TILE); m.floor(x1 - 5, y0, x1, y1, 1, S_TILE); m.floor(x0, y1 - 5, x1, y1, 1, S_TILE)
        for x in range(x0 + 12, x1 - 8, 14):
            for y in range(y0 + 12, y1 - 8, 14): m.solid(x, y, x + 2, y + 2, S_GREY)
        mx = (x0 + x1) // 2; my = (y0 + y1) // 2
        m.wall(mx - 1, y0, mx + 1, y1, S_BRICK2, gaps=((y0, y0 + 5), (y1 - 5, y1), (my - 6, my + 6)))
        m.lights(x0, y0, x1, y1, COOL, 11)
    hall(30, 20, 90, 80)                                                          # hall A
    m.floor(54, 44, 66, 56, 1, S_TILE); m.carrot(61, 51, 1)
    for (x, y, k) in ((40, 30, GOBLIN), (80, 70, OGRO), (60, 24, OGRO), (34, 72, RATA), (58, 52, SLITH)): m.mon(x, y, k, 90)
    m.item(86, 24, HEALTH); m.item(34, 26, BULLETS)
    m.door(90, 44, 92, 50, 1, 1, 12, S_WOOD); m.corridor(92, 44, 100, 44, 6, 1, 12, S_BRICK, S_TILE, S_BRICK2)   # the sluice
    hall(100, 20, 160, 80)                                                        # hall B
    for (x, y, k) in ((110, 30, GOBLIN), (150, 70, KNIGHT), (130, 24, OGRO), (154, 30, HELLPIG), (106, 74, HELLPIG)): m.mon(x, y, k, 270)
    m.floor(154, 57, 160, 69, -6, S_PEBBLE)                                          # a channel through the walk...
    m.room(160, 60, 180, 66, -9, -2, S_GREY, S_PEBBLE, S_GREY)                     # ...to the drowned passage...
    m.room(180, 52, 200, 76, -6, 12, S_BRICK, S_PEBBLE, S_BRICK2); m.floor(186, 54, 200, 64, 1, S_TILE)   # ...to a dry loft
    m.carrot(194, 58, 2); m.mon(190, 70, SLITH, 90); m.light(192, 60, 8, GREEN)
    m.door(124, 80, 130, 82, 2, 1, 12, S_WOOD); m.corridor(124, 82, 124, 100, 6, 1, 12, S_BRICK, S_TILE, S_BRICK2)
    m.room(100, 100, 170, 170, -6, 17, S_BRICK, S_PEBBLE, S_BRICK2); m.lights(100, 100, 170, 170, COOL, 11)   # hall C
    m.floor(100, 100, 170, 106, 1, S_TILE)                                          # its north walk
    x, y, f, right = 128, 107, 2, True                                              # stepping stones, zig-zag, a unit up each
    while x < 148 or y < 124:
        m.floor(x, y, x + 4, y + 4, min(f, 7), S_GREY)
        if right: x += 5
        else: y += 5
        right = not right; f += 1
    m.floor(x, y, x + 4, y + 4, 7, S_GREY)
    m.floor(152, 124, 170, 146, 8, S_TILE); m.carrot(162, 135, 3)                    # the far platform, against the wall
    for (x2, y2, k) in ((104, 150, OGRO), (166, 110, RHINO), (130, 160, GOBLIN), (150, 120, KNIGHT)): m.mon(x2, y2, k, 0)
    m.item(160, 102, HEALTH); m.item(104, 102, ROCKETS); m.item(158, 142, HEALTH)
    m.door(170, 132, 172, 138, 3, 8, 18, S_WOOD); m.corridor(172, 132, 186, 132, 6, 8, 18, S_BRICK, S_TILE, S_BRICK2)
    m.room(186, 132, 192, 152, 8, 18, S_BRICK, S_TILE, S_BRICK2)
    m.room(186, 150, 210, 176, 8, 18, S_BRICK2, S_TILE, S_PLANK); m.lights(186, 150, 210, 176, WARM, 10)
    m.carrot(204, 163, EXIT, 'xeno06'); m.mon(196, 156, BAUUL, 270); m.mon(196, 170, OGRO, 270)
    return m

def xeno06():
    """GAUNTLET - a long hall over deep water, crossed on narrow bridges under snipers.  Fall in and you swim back to the
    start of the stretch.  The last carrot opens the exit - and the brute's cage."""
    m = Map(128, 'Gauntlet - by %s' % AUTHOR, TECH, water=-4)
    m.room(6, 54, 20, 72, 0, 12, T_WALL, T_FLOOR, T_CEIL); m.start(9, 63, 270); m.lights(6, 54, 20, 72, COOL, 8)
    m.item(16, 56, CLIPS); m.item(16, 69, GREENARMOUR); m.item(10, 70, SHELLS)
    m.room(20, 30, 116, 96, -10, 13, T_WALL, T_GRATE, T_CEIL)                         # the drop, under water
    m.floor(20, 58, 26, 68, 0, T_PLATE)                                               # the first landing
    segs = [(26, 60, 44, 64), (44, 60, 48, 76), (48, 72, 66, 76), (66, 52, 70, 76), (70, 52, 88, 56), (88, 52, 92, 66), (92, 62, 110, 66)]
    for (x0, y0, x1, y1) in segs: m.floor(x0, y0, x1, y1, 0, T_PLATE)                # the bridges, 4 wide
    for (x0, y0, x1, y1) in ((20, 40, 24, 58), (20, 68, 24, 92)):                     # ladders back up at the start
        m.stairs(x0, y0, x1, y1, 0, -9, 'y' , T_GRATE)
    m.floor(20, 40, 24, 44, 0, T_PLATE)
    m.floor(110, 56, 116, 72, 0, T_PLATE)                                            # the far landing
    m.wall(56, 30, 58, 96, T_WALL, gaps=((70, 78),)); m.wall(84, 30, 86, 96, T_WALL, gaps=((50, 58),))   # three stretches
    for (x, y) in ((40, 34), (74, 34), (66, 92), (100, 92)):                          # sniper perches in the walls
        m.room(x - 2, y - 2, x + 3, y + 3, 6, 14, T_BLOCK, T_PLATE, T_CEIL)
    m.mon(40, 34, SLITH, 0); m.mon(74, 34, SLITH, 0); m.mon(66, 92, SLITH, 180); m.mon(100, 92, RHINO, 180)
    m.mon(46, 68, GOBLIN, 270); m.mon(68, 60, OGRO, 0); m.mon(90, 58, GOBLIN, 270)
    m.lights(20, 30, 116, 96, COOL, 16); m.item(56, 74, HEALTH); m.item(80, 54, BULLETS); m.item(100, 64, HEALTH)
    m.carrot(113, 64, 1)
    m.room(118, 56, 124, 72, 0, 12, T_BLOCK, T_PLATE, T_CEIL); m.door(116, 58, 118, 70, 1, 0, 12, T_DOOR)   # the cage
    m.mon(121, 64, BAUUL, 90); m.light(121, 64, 6, RED)
    m.door(108, 72, 116, 74, 1, 0, 12, T_DOOR); m.corridor(108, 74, 108, 84, 8, 0, 12, T_CONC, T_GRATE, T_CEIL)
    m.room(104, 84, 124, 100, 0, 12, T_WALL, T_FLOOR, T_CEIL); m.light(114, 92, 10, GREEN)
    m.carrot(114, 96, EXIT, 'xeno07'); m.item(106, 98, ROCKETS)
    return m

def xeno07():
    """STARFALL KEEP - a castle keep at night, its inner court open to the stars.  The levers have to go in order: the
    court's opens the great hall, the hall's the dungeon stair, the drowned dungeon's the chapel - each with company."""
    m = Map(256, 'Starfall Keep - by %s' % AUTHOR, STONE, water=-3, ambient=(26, 30, 46))
    m.room(40, 112, 62, 132, 0, 14, S_GREY, S_COBBLE, S_PLANK); m.start(44, 122, 270); m.lights(40, 112, 62, 132, WARM, 10)
    m.item(58, 114, CLIPS); m.item(58, 130, SHELLS)
    m.corridor(62, 119, 80, 119, 6, 0, 14, S_GREY, S_COBBLE, S_PLANK)                 # the gatehouse
    m.room(80, 90, 140, 150, 0, sky=True, w=S_BRICK, ft=S_GRASS)                      # the inner court, under the stars
    m.pool(100, 110, 120, 130, -8, S_PEBBLE); m.floor(106, 116, 114, 124, 0, S_TILE); m.carrot(110, 120, 1)
    for (x, y) in ((88, 98), (132, 98), (88, 142), (132, 142)): m.solid(x, y, x + 3, y + 3, S_GREY); m.light(x + 1, y + 5, 16, NIGHT)
    for c in (104, 134): m.wall(80, c, 140, c + 2, S_ROCK, gaps=((80, 88), (104, 116), (132, 140)))
    for (x, y, k, a) in ((92, 110, OGRO, 270), (128, 110, OGRO, 90), (110, 96, GOBLIN, 0), (110, 144, GOBLIN, 180), (96, 136, RATA, 0)): m.mon(x, y, k, a)
    m.item(84, 94, HEALTH); m.item(136, 146, BULLETS)
    m.room(84, 150, 96, 162, 0, 12, S_WOOD, S_WFLOOR, S_PLANK); m.door(86, 150, 94, 152, 1, 0, 12, S_WOOD)   # ambush 1
    m.mon(87, 157, OGRO, 0); m.mon(93, 157, OGRO, 0)
    m.door(104, 86, 116, 90, 1, 0, 16, S_WOOD)                                     # lever 1 -> the great hall
    m.room(90, 40, 130, 86, 0, 16, S_BRICK2, S_TILE, S_PLANK); m.lights(90, 40, 130, 86, WARM, 12)
    for x in (98, 120):
        for y in (50, 64, 76): m.solid(x, y, x + 2, y + 2, S_GREY)
    m.floor(104, 42, 116, 50, 4, S_WFLOOR); m.carrot(110, 45, 2)                    # the dais
    for (x, y, k) in ((94, 56, KNIGHT), (126, 56, KNIGHT), (110, 70, RHINO), (100, 80, GOBLIN), (120, 80, GOBLIN)): m.mon(x, y, k, 180)
    m.item(92, 84, ROCKETS); m.item(128, 42, HEALTH)
    m.door(130, 60, 132, 70, 2, 0, 14, S_WOOD)                                      # lever 2 -> the dungeon stair
    m.corridor(132, 60, 150, 60, 10, 0, 14, S_GREY, S_COBBLE, S_PLANK)
    m.stairs(150, 60, 156, 70, 0, -6, 'x', S_STEP)
    m.room(156, 40, 200, 90, -10, 8, S_ROCK, S_PEBBLE, S_ROCK); m.floor(156, 60, 162, 70, -6, S_STEP)   # the dungeon, flooded
    m.floor(190, 40, 200, 52, -2, S_TILE); m.carrot(195, 45, 3); m.light(194, 47, 8, GREEN)
    m.floor(170, 74, 180, 84, -2, S_TILE)
    m.lights(156, 40, 200, 90, DIMW, 14)
    for (x, y, k, a) in ((174, 78, SLITH, 90), (194, 80, HELLPIG, 90), (180, 48, HELLPIG, 270), (166, 50, GOBLIN, 0)): m.mon(x, y, k, a)
    m.room(184, 92, 198, 104, -2, 8, S_ROCK, S_PEBBLE, S_ROCK); m.door(186, 90, 196, 92, 3, -2, 8, S_WOOD)   # ambush 3
    m.mon(188, 98, BAUUL, 0)
    m.door(140, 130, 142, 140, 3, 0, 14, S_WOOD)                                   # lever 3 -> the chapel
    m.corridor(142, 130, 160, 130, 10, 0, 14, S_GREY, S_COBBLE, S_PLANK)
    m.room(160, 116, 196, 156, 0, 15, S_BRICK2, S_TILE, S_PLANK); m.lights(160, 116, 196, 156, (200, 170, 255), 12)
    m.floor(184, 130, 196, 142, 3, S_WFLOOR); m.carrot(190, 136, EXIT, 'xeno08')
    for (x, y, k) in ((170, 122, KNIGHT), (170, 150, KNIGHT), (182, 126, OGRO), (182, 146, OGRO)): m.mon(x, y, k, 90)
    m.item(164, 152, HEALTH); m.item(164, 120, GREENARMOUR)
    return m

def xeno08():
    """XENOCIDE - the last stand.  An arena round a drowned pit; the first carrot lets out the first wave and raises the
    way to the island, the island's lets out the second and opens the gate, and the gate's carrot ends it."""
    m = Map(128, 'Xenocide - by %s' % AUTHOR, TECH, water=-2)
    m.room(56, 104, 72, 122, 0, 12, T_WALL, T_FLOOR, T_CEIL); m.start(64, 118, 180); m.lights(56, 104, 72, 122, COOL, 8)
    m.item(58, 106, CLIPS); m.item(70, 106, SHELLS); m.item(58, 120, GREENARMOUR); m.item(70, 120, HEALTH)
    m.room(24, 30, 104, 104, 0, 16, T_WALL, T_CFLOOR, T_CEIL)                          # the arena
    m.pool(44, 50, 84, 84, -9, T_GRATE); m.floor(56, 60, 72, 74, 1, T_PLATE)          # the pit and its island
    m.carrot(32, 96, 1); m.carrot(64, 67, 2)
    for (x, y) in ((34, 40), (88, 40), (34, 86), (88, 86), (60, 40), (60, 90)): m.solid(x, y, x + 6, y + 6, T_BLOCK)   # cover
    m.lights(24, 30, 104, 104, (255, 160, 120), 12)
    for (x, y, k) in ((30, 40, RHINO), (98, 40, RHINO), (30, 70, OGRO), (98, 70, OGRO), (64, 36, KNIGHT)): m.mon(x, y, k, 0)
    m.item(100, 100, ROCKETS); m.item(28, 34, BULLETS); m.item(100, 34, HEALTH)
    for (x0, y0) in ((8, 40), (8, 76)):                                               # wave 1: the west pens
        m.room(x0, y0, x0 + 14, y0 + 14, 0, 12, T_BLOCK, T_CFLOOR, T_CEIL); m.door(x0 + 14, y0 + 4, x0 + 16, y0 + 10, 1, 0, 12, T_DOOR)
        m.mon(x0 + 5, y0 + 5, OGRO, 270); m.mon(x0 + 9, y0 + 9, GOBLIN, 270); m.light(x0 + 7, y0 + 7, 7, RED)
    for (x0, y0) in ((106, 40), (106, 76)):                                           # wave 2: the east pens
        m.room(x0, y0, x0 + 14, y0 + 14, 0, 12, T_BLOCK, T_CFLOOR, T_CEIL); m.door(x0 - 2, y0 + 4, x0, y0 + 10, 2, 0, 12, T_DOOR)
        m.mon(x0 + 5, y0 + 5, KNIGHT, 90); m.mon(x0 + 9, y0 + 9, RHINO, 90); m.light(x0 + 7, y0 + 7, 7, RED)
    m.door(56, 28, 72, 30, 2, 0, 16, T_DOOR)                                          # the gate
    m.room(48, 8, 80, 28, 0, 16, T_WALL, T_FLOOR, T_CEIL); m.lights(48, 8, 80, 28, GREEN, 10)
    m.mon(56, 14, BAUUL, 0); m.mon(72, 14, BAUUL, 0)
    m.carrot(64, 12, EXIT, ''); m.item(52, 24, HEALTH); m.item(76, 24, HEALTH)
    return m

# ---- multiplayer ----------------------------------------------------------------------------------------------------
def xdm1():
    """PUMPROOM - a small deathmatch room round a pool, its corners raised, two pads up"""
    m = Map(64, 'Pumproom - by %s' % AUTHOR, TECH, water=-1)
    m.room(6, 6, 58, 58, 0, 18, T_WALL, T_CFLOOR, T_CEIL)
    m.pool(24, 24, 40, 40, -7, T_GRATE)
    for (x, y) in ((6, 6), (48, 6), (6, 48), (48, 48)): m.floor(x, y, x + 10, y + 10, 5, T_PLATE)
    m.pad(19, 32, 26); m.pad(44, 32, 26)
    m.solid(30, 12, 34, 16, T_BLOCK); m.solid(30, 48, 34, 52, T_BLOCK)
    for (x, y, a) in ((11, 11, 315), (52, 11, 45), (11, 52, 225), (52, 52, 135), (32, 20, 0), (32, 44, 180)): m.start(x, y, a)
    m.item(11, 11, ROCKETS); m.item(52, 52, BULLETS); m.item(32, 32, QUAD); m.item(11, 52, SHELLS); m.item(52, 11, HEALTH)
    m.item(20, 20, CLIPS); m.item(44, 44, CLIPS); m.item(44, 20, GREENARMOUR); m.item(20, 44, HEALTH)
    m.lights(6, 6, 58, 58, COOL, 10)
    return m

def xdm2():
    """CROSSFIRE - two bases face each other across a court open to the sky; a hill in the court, a pool beneath it"""
    m = Map(128, 'Crossfire - by %s' % AUTHOR, STONE, water=-2)
    for (x0, team) in ((8, 0), (92, 1)):
        m.room(x0, 44, x0 + 28, 84, 0, 16, S_BRICK2, S_TILE, S_PLANK)
        fx = x0 + 6 if team == 0 else x0 + 21
        m.flag(fx, 64, team); m.lights(x0, 44, x0 + 28, 84, RED if team == 0 else COOL, 10)
        for k in range(4): m.start(x0 + 10 + (k % 2) * 8, 52 + (k // 2) * 24, 270 if team == 0 else 90); m.ents[-1] = m.ents[-1][:5] + (team,) + m.ents[-1][6:]
        m.item(x0 + 14, 46, ROCKETS if team == 0 else BULLETS); m.item(x0 + 14, 82, HEALTH); m.item(x0 + 14, 64, CLIPS)
    m.room(36, 30, 92, 98, 0, sky=True, w=S_BRICK, ft=S_GRASS)                     # the court
    m.room(36, 56, 36, 72, 0, 14); m.room(92, 56, 92, 72, 0, 14)
    m.floor(54, 54, 74, 74, 3, S_TILE); m.hill(64, 64, 10, 10)                      # the hill, a step up
    m.pool(58, 36, 70, 48, -7, S_PEBBLE); m.pool(58, 80, 70, 92, -7, S_PEBBLE)
    for (x, y) in ((44, 40), (82, 40), (44, 86), (82, 86)): m.solid(x, y, x + 3, y + 3, S_GREY)
    m.item(64, 64, SHELLS); m.item(64, 42, GREENARMOUR); m.item(64, 86, HEALTH)
    m.lights(36, 30, 92, 98, (220, 220, 200), 14)
    return m

def xdm3():
    """NIGHT YARD - a small yard under the stars, a raised hill in the middle, a well in each corner"""
    m = Map(64, 'Night Yard - by %s' % AUTHOR, STONE, water=-2, ambient=(26, 30, 46))
    m.room(6, 6, 58, 58, 0, sky=True, w=S_GREY, ft=S_COBBLE)
    m.floor(24, 24, 40, 40, 4, S_TILE); m.stairs(18, 28, 24, 36, 0, 4, 'x', S_STEP); m.stairs(40, 28, 46, 36, 4, 0, 'x', S_STEP)
    m.hill(32, 32, 8, 8)
    for (x, y) in ((8, 8), (50, 8), (8, 50), (50, 50)): m.pool(x, y, x + 6, y + 6, -7, S_PEBBLE)
    for (x, y, a) in ((12, 32, 270), (52, 32, 90), (32, 12, 0), (32, 52, 180), (20, 20, 315), (44, 44, 135)): m.start(x, y, a)
    m.item(32, 32, ROCKETS); m.item(12, 20, SHELLS); m.item(52, 44, BULLETS); m.item(20, 52, HEALTH); m.item(44, 12, HEALTH)
    m.item(32, 20, CLIPS); m.item(32, 44, CLIPS)
    for (x, y) in ((16, 16), (48, 16), (16, 48), (48, 48), (32, 32)): m.light(x, y, 12, NIGHT)
    return m

MAPS = {'xeno01': xeno01, 'xeno02': xeno02, 'xeno03': xeno03, 'xeno04': xeno04, 'xeno05': xeno05, 'xeno06': xeno06,
        'xeno07': xeno07, 'xeno08': xeno08, 'xdm1': xdm1, 'xdm2': xdm2, 'xdm3': xdm3}

if __name__ == '__main__':
    out = sys.argv[1]; names = sys.argv[2:] or list(MAPS)
    os.makedirs(out, exist_ok=True); bad = 0
    for name in names:
        m = MAPS[name]()
        ok, rep = m.check(verbose=True)
        m.write(os.path.join(out, name + '.cgz'), DIR); m.preview(os.path.join(out, name + '.png'))
        print('%-7s %s  %dx%d, %d ents  %s' % (name, 'OK ' if ok else 'BAD', m.N, m.N, len(m.ents), rep))
        bad += not ok
    sys.exit(1 if bad else 0)
