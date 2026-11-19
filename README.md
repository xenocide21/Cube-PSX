# Cube PSX

A single-player PlayStation 1 port of **Cube** (Wouter van Oortmerssen, 2001-2005), built with
[PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK). Version 0.23.2.

## Running it
Boot `cubepsx.cue` in an emulator, or burn `cubepsx.bin/.cue`. Maps, models and sky are files on the disc, so
`CUBEPSX.EXE` alone will not work. 320x240, NTSC timing.

## Controls
| | |
|---|---|
| Left stick / D-pad up-down | move |
| Right stick / D-pad left-right | look / turn |
| L1 / R1 | strafe left / right |
| R2 or L2 | fire |
| Cross | jump |
| Triangle / Square | next / previous weapon (fist, pistol, shotgun, chaingun, rockets, rifle) |
| Start | pause menu (resume, save, options, quit to menu); restart after dying; next map after an exit |
| Select | automap on / off |

**License and credits** (v0.23.2): `LICENSE.txt` and `CREDITS.txt` go with every download, and `licenses/` holds
every original readme the game media came with (and the GPL models' original files). In short: the program is an
altered version of Cube's source, under Cube's own license (zlib plus "no relicensing"); the maps, textures, models,
sounds and music keep their authors' terms, which together allow free, non-commercial, downloadable distribution with
credit - no selling, no pressed discs. Wouter van Oortmerssen has seen the port and offered to link it on the Cube site.

**Giga Stacy** (v0.23.0): a second character, chosen when a profile is made (after the name) and changed any time from
the hub (CHARACTER); kept in the profile. She is the dude's counterpart: the status bar portrait drawn the same way and
animated the same way (`tools/mkstacy.py`: big wavy blonde hair, gold aviators with lined eyes and lashes behind the
tint, red lips, gold hoops, strong jaw and shoulders, a black sports top; she smiles, grits her teeth when hit, grins at
a kill - no cigar), and the same voice lines tuned into a deep female voice (`tools/voicefem.py`: pitch and formants up
19% together, then the pitch alone 12% more, timing untouched; `assets/voice_f`, `maps/voice_f.pak`). The portraits are
`WHO0.BIN` / `WHO1.BIN` and the voices `VOICE.PAK` / `VOICE_F.PAK`, each loaded over the other when you choose (the sound
chip has room for one voice bank). Profiles from before have Giga Chad. (v0.23.1: loading her face or voice used the buffer
the title demo's map lives in, and the map was then taken as still loaded - the demo went wrong and a map started after
it could crash; now anything else loaded there makes the map load again (`world_forget`). And every disc read now
pauses the music stream first: right after it the drive could hand over music sectors in place of the file - which is
how a map's textures could come out missing.) Disc note: the BIOS (and emulators) look for
`SYSTEM.CNF` in the first sector of the root directory, so new files on the disc should sort after it (hence `WHO`).

**Match menu** (v0.22.1): START in any multiplayer match shows the table - every player and bot with frags, deaths
and (CTF) flags brought home, your row marked, best first (by team in team games), with the team score - and RESUME,
OPTIONS, QUIT MATCH. On one console the match pauses under it. In a link game it does not (the other player is still
playing; your pad does nothing while it is up) and there are no options (both consoles must run the same game); QUIT
MATCH ends the match on both consoles with the other player the winner ("PLAYER 2 LEFT - PLAYER 1 WINS"). When you are
fragged in a link game, START still puts you back in.

**Link cable play** (v0.22.0): two consoles, a player each, joined by the serial link cable (Multiplayer > Link Cable).
Both go to the Link Cable screen; it says when it hears the other console. Either player picks the mode (deathmatch,
team deathmatch, CTF, King of the Hill), map, bots (0-6), teams (P1 red v P2 blue, or both red against the bots) and
skill, and presses START; the other console joins, both load the map, and the warm-up waits until both players have
pressed fire. In deathmatch the other player is drawn green. START after the match goes back to the Link Cable screen.
* How: both consoles run the whole game, the same tick on both, from both pads (`src/link.c`). A pad goes down the
  cable four ticks before it is used (14 bytes a tick at 115200 baud), and a tick runs once both pads for it are in.
  Every 64 ticks each console sends a hash of the game; if they ever differ, the match stops ("out of step").
* The second player is kept beside the first and swapped into `pl` for its part of each tick (`src/linkplay.inc`):
  the player code - movement, guns, pickups, teleports, damage - serves both. Everything else sees player 2 through a
  stand-in bot (`bot == 2`) that does not think: bots aim at it, shots hit it (the damage is passed on to player 2),
  it carries flags and holds the hill. The guest console swaps player 2 in to draw its screen and hear its sounds.
* Bots see as far as 24 cells in a link game (each console's own view distance can differ; the game must not).
* The host's move and turn speeds are used by both (they also set the bots' pace); look speed, invert and aim assist
  are each player's own.
* `test/link_test.c`: the warm-up waits for both; each player can frag the other; bots go for player 2; and a
  100-second CTF with two restless players and four bots gives the same game, tick by tick, seen from either console.
* For emulators without a link cable there is **PSLink** (a separate download): two PlayStations side by side in one
  window, linked, controller 1 and 2 (and keyboard) on console 1 and 2.

**XENO - the PS1-exclusive map pack by Xenocide21** (v0.21.0): an eight-map campaign (Single Player > XENO) and three
multiplayer maps (in Quickplay), built for the PlayStation: mostly closed rooms of moderate size (sight lines kept to
what the PS1's view distance shows), ceilings low enough to see, a few dozen monsters a map, water in every map, and
three night maps under the star sky, lit darker.
* 1 Intake (128): a pumping station - a carrot that opens the way on and the monster closet behind you, crates hiding a
  carrot, a jump pad to the control platform.
* 2 Flooded Works (128): four flooded bays with walkways and islands; the key is in a chamber only an underwater tunnel
  reaches, and taking it lets the hellpigs out of their pens.
* 3 Night Courtyard (256, night): castle grounds round a fountain, hedged into courts; three towers each keep a carrot
  (up a stair, up a jump pad, in a guarded loft that opens a stable of ogres); the keep's gate has three locks.
* 4 The Vaults (128): a tomb whose sanctum has four seals - an island in a flooded vault, a hidden room behind a
  teleporter, the top of a stepped pyramid, a sarcophagus that wakes its guards.
* 5 Cisterns (256): flooded halls with pillar arcades, a drowned passage to a dry loft, stepping stones rising out of
  the water.
* 6 Gauntlet (128): narrow bridges over deep water in three stretches, snipers in the walls; fall in and you swim back;
  the last carrot opens the exit and the brute's cage.
* 7 Starfall Keep (256, night): levers in order - the starlit court, the great hall, the flooded dungeon - each with
  company, to the chapel.
* 8 Xenocide (128): the last stand - an arena round a drowned pit, two waves from the pens, the gate.
* Multiplayer: Pumproom (64, deathmatch), Crossfire (128, two bases: CTF, and a hill in the court for King of the
  Hill), Night Yard (64, night: deathmatch, King of the Hill).

Made by `tools/mkxeno.py` on `tools/xmap.py` (rooms, doors, pools, stairs, pads, teleporters, monsters; it plays each
map through - steps, jumps up to 4, drops, swimming, each carrot's doors - before writing it); `test/xeno_test.c`
checks the same in the game (every carrot opens its doors, the exit ends the level). A map's `.cfg` may carry
`// ambient r g b` (ours; Cube skips it) for its base light. The maps for Cube on the PC are in `XENO-for-Cube-PC`
(put the folder in Cube's packages as `xeno`). Music: tracks of the album, one per map.

**King of the Hill** (v0.20.0): a fourth quickplay mode, red against blue, on Facing Cubes (the map has to have a hill:
our entity 20, `E_HILL`, centred on it with half its size in x and y - Facing Cubes' is the middle platform, 16 x 24
cells). A team alone on the hill scores a point a second; both there, nobody scores; first to 60. A flag stands in the
middle in the holder's colour (white: nobody; flickering red-blue: contested), and the line under the score says who
holds it. The bots all go for the hill and fight over it.

**Quickplay warm-up** (v0.19.5, no-clip v0.20.0): after a quickplay map loads, it is yours alone - fly about through
anything (you go where you look; X rises) - with no bots and nothing to pick up, until you press fire (R2 / L2). Then
you go to a start point on your feet, every bot comes in at once and the match is on; the map is already loaded, so
there is no wait.

**Quickplay warm-up, as first made** (v0.19.5): after a quickplay map loads, it is yours alone - run about, try the teleporters and
jump pads, learn the drops (a fall just puts you back) - with no bots and nothing to pick up, until you press fire
(R2 / L2). Then you go to a start point, every bot comes in at once and the match is on; the map is already loaded, so
there is no wait.

**Bots and monsters** (v0.19.4):
* Bots miss like people: each shot is a hit or a clean miss (past the body, not just off its middle). At best (close,
  you standing still, aim settled) 30 / 40 / 52 % hit on easy / normal / hard; a new target takes 0.8-1.5 s to settle
  on, a moving one is about half as easy, distance costs more, a bot being hit flinches, and the chaingun goes in
  bursts of 5-8 with a breath between. A normal bot needs about 5 s to finish you at mid range if you keep moving.
* Bots see no further than you do (your view distance at that moment), react slower to you out in the fog band, and
  do not see behind them (about 70 degrees either side) unless you are close, they are already fighting you or you
  just hit them.
* Monsters notice you sooner: Cube's ranges doubled (16 units even with their back to you), and anything in plain
  sight within your view notices you after a moment - half a second facing you, two with its back turned.

**Facing Cubes view distance** (v0.19.3): Cube hides only what is behind solid walls, and Facing Cubes has almost none -
its temples, arms and platforms are raised floors - so everything behind them was drawn anyway. On that map the renderer
now keeps a ground horizon (per direction, the highest line nearer ground already covers) and skips whatever stays
under it, with all it holds; a square something stands in gets 8 more units. Far ramp steps of one unit are drawn as
2x2 squares past 28 cells. Checked pixel for pixel against the old renderer. Cube's own maps are unchanged (their rooms
gain nothing from it).

**Jumping** (v0.19.2): now Cube's own - about 4.3 units high and 0.7 s in the air (it was a third of that before, which
left pig04's carrot pens, with their 4 high ledges, out of reach). Leaving the water keeps the stroke's lift, as in Cube.

**Pistol** (ours, v0.19): a weapon of its own between the fist and the shotgun - one bullet a shot every 0.55 s, 22 damage
(the shotgun does up to 10 x 12 every 1.4 s). It is not unlimited: boxes of pistol rounds (15 a
box, 60 at most; an olive crate with brass on the lid) stand on every map - `cgz2cmp.py` puts one beside every second
ammo pickup of Cube's maps and one near the start, on open level floor clear of teleporters. A box picked up with only
the fist in hand switches to the pistol. Model and skin are ours (`tools/mkmodel.py`, `assets/pistolskin.png`,
`assets/clipskin.png`). Saved games from before the pistol still load (their ammo gets an empty pistol slot).

Status bar, last line: `VIEW` = current view distance in cells (max 72), `POLYS` = polygons this frame,
`T` = percent of the 30 fps frame time used.

## Front end (`src/menu.c`, `src/save.c`)
* Splash screen (Cube logo, Cube's own font from `data/newchars.png`), then the title menu over an attract demo:
  a generated walk through cruel01 (`tools/mkdemo.py` -> `src/demo_path.h`) in which the demo player turns to and
  shoots monsters it meets. X selects, O backs out, Quake-style spinning cursor.
* NEW GAME: pick one of 3 memory card slots, type a name on the on-screen keyboard, pick the difficulty (the announcer
  calls it), then the hub. LOAD GAME: pick a profile, then the hub.
* Hub (after a profile is made or loaded): Single Player / Quickplay / Multiplayer / Options / Quit to Title. Single Player lists the
  campaigns with your progress in each (maps beaten / maps, and where your saved game is); picking one opens a list of
  just that campaign's maps. Multiplayer is a placeholder for the bots and link cable to come.
* Profiles: a 2-block card file per slot, `BASCUS-99999CUBEn` - the standard header with the Cube icon, then the profile
  (name, difficulty - Cube's `skill` 1 / 3 / 7 plus monster damage 60 / 100 / 140% - the options, and per map: open,
  beaten, all kills, best time, most kills), then optionally a game saved from the pause menu (map, player, monsters,
  items, carrots, opened doors).
* Campaign map lists: Cube's single-player campaigns in Cube's own order (its `level_trigger` "map next" scripts): PIGCAM by
  Jonathan Whiting (pig01-pig09, pigfin), CRUEL by unicious (cruel01-02), WHY SO GREEN by SirLiveAlot (wsg1-3). The first
  map of each campaign is open; beating a map opens the next. All 15 maps are in. pig06 is a 512x512
  map, but every room in it stands in its top-left corner inside an empty field nobody can reach; the converter crops it
  to that 256x256 corner (`cgz2cmp.py ... --crop 16,0,256`), keeping all 146 entities. A finished map is recorded and saved to the card
  at once; START goes back to the selector with the next map under the cursor. A map with a saved game resumes it (X)
  or starts over (SQUARE).
* No memory card: a warning, then play goes on as normal (profile, map unlocks, pause saves all work for the session);
  nothing survives power off. Messages say so where something would have been saved.
* Automap (SELECT): Doom's map on Cube's grid - everything you have seen, from above, centred on you and north up, 2
  pixels a cell. Red = walls, brown = drops of more than a step, an arrow for you, the exit (magenta) and teleporters
  (cyan) once seen. The game goes on while it is up (no gun or crosshair). What you have seen is one bit per 2x2 cells,
  marked by the renderer for every near square it draws and by the game around you; it is kept in saved games. The
  map is mirrored in x on purpose: in Cube's world, facing -y puts -x on your right. The wall layout is worked out
  again only when you step into another cell or see something new.
* Pause: Resume / Save Game (into the profile) / Options / Quit to Map Select / Quit to Title.
* Options: Controls (move speed, turn speed: 1 = the PS1 tuning, 40% of Cube's running speed, 10 = Cube's own 24 u/s;
  monsters scale with move speed; turn 10 = the full v0.2 turn rate; look speed for the right stick's up/down, 1 = the
  old tuning, 10 = 5.5x; invert look), Sound (effects, speech and music volume) and Gameplay (aim assist: the Doom-style
  helper that bends shots toward a monster near the crosshair; turn it off to aim freely with the stick). Kept in the
  profile (profile version 2; a v0.10 profile loads with the new options at their defaults).
* Assets: `tools/mkui.py` -> `maps/ui.pak` (font, logo, card icon, portrait), from `assets/cube_logo.png`.

## Quickplay (`src/bots.inc`, `tools/mkfacing.py`, `tools/mkspace.py`)
You against bots, still single player. Mode: Deathmatch (to 15 frags), Team Deathmatch (to 20), Capture the Flag (to 3).
Map: Facing Cubes (CTF and the deathmatches) or one of Cube's 128x128 single-player maps (deathmatches; their monsters
stay home, their doors are open, the monsters' places are extra spawn points). Bots 1-7; in team modes you are red and the
bots alternate blue, red, ... (3 bots = you + 1 against 2). Skill = Easy / Normal / Hard (bot aim, reaction time, and the
usual damage scaling). No saving; Quit to Map Select goes back to the Quickplay screen.

* **Bots** are monsters that play like you: drawn as Cube's ogro (Cube's player model), tinted red / blue in team modes;
  moving at 55% of a player's run (30% while fighting or strolling), turning at Cube's monster rate (no wild spinning,
  a slow sidestep in a fight); player guns at full damage, starting with fist + pistol (20 rounds) like you, picking up ammo and health (items come back after
  16-25 s in quickplay), respawning 3 s after dying. Choice of gun by range (rockets at mid range, the rifle far, the
  shotgun close, else the pistol; the fist only up close with nothing left). A new enemy needs to be in sight a moment before the first shot (0.7 / 0.4 / 0.25 s by skill) and
  the aim wanders by 16 / 11 / 7 % of the distance.
* **Getting about**: distance maps. For a goal, every cell a body fits in holds its number of steps to the goal (one
  step up a unit at most, any drop, teleporters as one-step shortcuts), worked out backwards from the goal; a bot walks
  downhill, keeping away from edges. Four maps of 128x128 bytes in the memory the map leaves free (a 128 map uses a
  quarter of the map area): the two flag bases (deathmatch: two roaming spots, items), and one moving goal per team
  (a stolen or dropped flag, or the human). A body is 2.2 cells across, so only cells it can stand in count.
* **CTF**: touch their flag to take it, touch yours at home while carrying theirs to score; a dropped flag returns
  when its team touches it or after 30 s. Jobs: each team keeps one defender per two opponents (blue always keeps
  an attacker; on red you count as one), the rest attack. Defenders patrol within a few cells of their flag, hunt a
  thief and return a dropped flag; attackers go for the enemy flag; everyone escorts a carrier home. The score is shown at the top; the dude has lines for scores.
* Ledges: a body on a ledge overhangs the cells just below its edge, so the distance maps also link a ledge to the
  ground 2-3 cells out over open space (a drop), and a bot that steps off commits to it.
* **Falling** into space kills (below 60 units down); a fall or your own rocket costs a frag.

### Facing Cubes - a Cube take on Unreal Tournament's Facing Worlds
Built by `tools/mkfacing.py` as a real Cube map (`assets/fcubes/fcubes.cgz` + `.cfg`, loads in Cube itself with its own
Egyptian textures - drop both in `packages/base`). Two temples on islands in space and a raised platform with cover in
the middle. From each temple two bridge arms swing wide, north and south, and climb a unit at a time from the island
(height 0) to the middle (12): about 75 cells from a temple door to the middle, uphill all the way, open on every side -
and downhill all the way home with their flag. A walkway runs all round each temple; open space in front of the doors.
Cube's world is a height field - a cell has one floor and one ceiling - so the temple's levels are stepped, not stacked:
* Hall: entrance facing the bridge; the pillar ahead (8x8, a 3-wide way in from its back, the teleporter to the roof
  inside); on the left the teleporters to level 2 (near the door) and level 3 (at the back); the flag on the right.
  A teleporter fires within 4 units (Cube), so they stand well apart and no spawn is near one.
* Roof (48): the front of both flanks, either side of the tall middle - the rifle and its rounds; level 3 (32) behind
  it on the left - the chaingun and green armour; level 2 (16) across the back - the rocket launcher. Each level has a
  teleporter back down to the ground in front of the temple (or jump).
* The shotgun lies outside, along each side wall of the temple; pistol rounds at the start points and by the ammo.
* The sky: our own space box (`tools/mkspace.py`, ray traced: stars, a planet below with oceans, land, clouds and a
  blue rim of atmosphere, the sun far off), with a palette that always keeps black, the star whites and the sun.
  The abyss floor and the map's border are "sky" (texture 0), and past the view distance open space is left open
  instead of the black stand-in Cube's enclosed maps get - so things fade into space, not into a black wall.
* The flags: a pole and a waving cloth we made (`tools/mkmodel.py`), white, tinted with the team colour.
* (A parody of the layout; nothing of Epic's is used.)

## Status bar portrait (`tools/mkface.py`, `draw_face` in `src/render.c`)
* A 44x54, 16-colour pixel-art tough guy drawn in code: pompadour, square stubbled jaw, gold aviators with his eyes
  showing through the tint, a cigar.
* Built from layered sprites so it animates for almost nothing: a head per health level (100-75, 74-50, 49-25,
  under 25, each more beaten up) plus a death face; an eye layer (glances left / right every few seconds, blinks,
  squints when hit); a mouth layer (cigar, gritted teeth when hit, a smirk after a kill); the ember glows up for a
  draw now and then and lets out a see-through puff of smoke.
* VRAM: 256x128 4bpp at (960,384) (the free half of texture page (960,256)), CLUT at (544,241).
  `mkface.py out.bin --sheet preview.png` writes the data and a preview; it refuses layers that would not line up.

## What is Cube and what is not
Taken from Cube's source and data:
* **World rendering** (`src/render.c`): a port of `worldrender.cpp` / `rendercubes.cpp` - the mip quad tree with
  "perfect mip" merging (`world.c` ports `remip`), floors, ceilings, walls, corners and height fields with Cube's rules.
* **Lighting**: `tools/cgz2cmp.py` ports `worldlight.cpp` (`calclight`) and stores Cube's per-vertex light in the map;
  it is drawn gouraud shaded with Cube's x2 overbright. One deliberate change: +20 ambient (`AMBIENT_LIFT`), because the
  PS1's 15-bit colour turns Cube's ambient level into pure black.
* **Collision** with solids, corners and height fields (`world_fits` ports the map part of `collide`).
* **Doors, carrots, teleports, jump pads** (`game.c`, ports of `settag`/`trigger`, `pickup`, `teleport`, `entinmap`):
  tagged areas start closed and a carrot opens its tag; carrot type 2 ends the level; teleports take you (and monsters)
  to the TELEDEST with the matching tag; jump pads add Cube's push. Pickup radii (2.5 units, 4 for a teleport) and the
  500 / 300 ms cooldowns are Cube's. Reaching the exit or killing every monster ends the level (`endsp`): press START.
  Cube's map scripts are not run; the one thing the maps here use them for, `level_trigger_N [map next]`, is detected
  by `cgz2cmp.py` and makes that carrot an exit.
* The carrot and teleporter models are drawn as in `renderentities`.
* **Monster AI** (`game.c`, a port of `monster.cpp`): Cube's state machine - sleep, home, aiming, attacking, pain,
  search - with its transition timing per monster type (lag, rate, pain) shortened by `skill`. Sleeping monsters wake on
  sight, seeing further the more directly they face you (8 units behind them, up to 64 in front, any distance within
  10 degrees). They fire at where you were when they started aiming (step aside to dodge), at a quarter of the weapon's
  damage; their projectiles fly at half speed and burst where they were aimed. A hit stuns them (no firing). Monsters
  in the line of fire get hurt too and, past their `loyalty`, turn on whoever hit them (infighting; those kills count).
  Blocked by scenery they random-walk (turn 180-360 degrees) or, rarely, jump; big monsters will not step off a drop
  of more than 4.5 units. Bodies block each other and you (a move is only refused if it brings two bodies closer, so
  nothing gets stuck). Cube checks sight every frame; sleeping monsters here look every 8th tick (133 ms).
* **Water** (pig09's canal, wsg1's pool): Cube's `moveplayer` water rules - you are in it while the level is above eye
  height - 0.5; half speed, forward follows your view up and down, you sink 2.5 units a second, and jump is a stroke
  (Cube's vel.z 1.7 with water friction: about 7 units) that bursts you out onto a ledge. Monsters swim at half speed
  towards their enemy. The surface is Cube's: 0.3 units under the level, over every square whose floor is below it,
  corners bobbing on sin(x*y*0.1 + t/300 ms) * 0.2, the map's liquid texture (slot 1, packed by `mktex.py` as a
  see-through 16-colour entry) drifting a quarter cell a second - here by uploading it shifted. Drawn 50/50
  see-through in walk order: after a square's walls and floor, before what stands on it. Under water the view closes
  in (24 cells) behind a blue-green layer. A height field square only gets a surface when all its corners are under.
* **Sound** (`src/sound.c`, `tools/mksound.py` -> `maps/sound.pak`): all 51 of Cube's sounds, numbered as in
  `data/sounds.cfg`, trimmed and encoded as SPU ADPCM at their own rates (11 / 22 kHz): 343 KB, loaded into sound RAM at
  start-up (about 170 KB stays free for voice lines). Played where Cube plays them: weapons (yours and monsters'),
  rocket and fireball bursts, each monster type's pain and death sounds, wake-up grunts, your pain and death, pickups,
  door rumble, teleports, jump pads, jump / land, splashes, weapon switch. Positioned sounds use Cube's rules
  (`updatechanvol`): volume falls linearly to nothing at 85 units, pan follows the angle to you, both updated every
  frame; at most 5 new sounds a frame. A shotgun blast is one hit per target (pellets added up), as in Cube. Voices
  have headroom (about 56% each) for chaingun bursts. The Sound Volume slider works.
* **Voice** (`assets/voice`, cut and processed from the author's recording by `tools/voicecut.py`: rumble and hiss
  removed, the dude 3 semitones deeper with a bigger chest, the announcer 4, levelled like broadcast speech - words
  evened out, compressed, limited - and the shit-shit-shit echo built in) in `maps/voice.pak`, on its own sound channel
  at full volume. While someone talks the other sounds duck: to 20% for the announcer, 60% for the dude. The dude grunts when hit (at most every 0.4 s), yells when he
  dies, throws a line on about one kill in three (at least 4 s apart, never the same twice running, just after the
  death cry; big hits favour "That looks like it hurt"; infighting kills get nothing), complains once at each closed
  door he walks into (at most every 30 s), and says "Clean sweep" / "Who's next?" when a map is cleared / exited. The
  portrait's jaw works while he talks. The announcer calls the difficulty when you change it in Options.
  Licences: Cube's readme puts the "aard" sounds in the public domain and the "free" ones as free to use; the "awesund"
  ones (rifle, chaingun, rockets, explosions, fireball) come from a Sin sound replacement pack with its own readme.
* **Models**: Cube's MD2 monsters, items and hud guns; the gun is drawn at the eye as Cube does.
* Weapon, monster and item numbers (`game.c`) from `weapon.cpp`, `monster.cpp`, `entities.cpp`.

PS1 specific:
* Visibility is a strict front-to-back walk with a 360 degree angular occlusion buffer; draw order is the walk order
  (no Z buffer). Polygons too close or too far off screen for the GTE are clipped and projected in software, rounded
  exactly like the GTE.
* Fog fades to black over the last 3/8 of the view distance instead of Cube's fog colour. The view distance adapts
  (20-72 cells) to hold 30 fps.
* Textures are 128x128; the most used ones get 256 colours, the rest 16 (48 quarter-page VRAM budget).
* Maps up to 256x256. The map file's own 12-byte cells are the level-0 squares (no second copy), the mip levels follow
  them, and the texture pack shares the same 1 MB area: it goes to video memory first, then the map is loaded over it.
  The renderer's per-frame tables (corner angle cache, monster-per-cell index) are windows of about 160x160 cells that
  follow the viewer. Distances use overflow-safe lengths (`vlen2`/`vlen3`): on a 256 map a squared distance passes 2^31.
* First-person guns: no depth buffer, so `tools/gunorder.py` works out a correct draw order for each animation frame
  with a depth buffer at conversion time (stored in `models.pak`).
* Merged squares are limited to 8x8 cells and to 3x their size in distance (affine texturing); `LIGHTERR` is 64, not 24.

* **Music** (`src/music.c`, `tools/mkmusic.py` -> `MUSIC.XA`): a track for every map, Doom style, and one for the menus,
  all from Cube's own release: the Fanatic album by Marc A. Pullen (11 tracks + Revenge), cubez0r by DaMaul, and
  cube1 / ambient / deathB by Lu-Wang (General MIDI files, rendered with `tools/midi2wav.py` and a GM soundfont).
  Not used: DCP's cubetrak (Cube plays it on the WSG maps), whose readme asks permission for redistribution.
  CD-XA ADPCM, 37.8 kHz stereo, made by our own encoder (`tools/xaenc.c`: every 28-sample block tries the 4 XA filters
  and all shifts against the decoder's own state; ~36 dB SNR). The 16 tracks are laid end to end in 8 interleaved
  channels (balanced, so the file is 116 MB rather than 160) and the drive streams them at 2x straight into the SPU's
  CD input: no CPU, no sound RAM. Looping is by the clock (each track has a second of silence after it); loading a map
  takes the drive away, after which the track picks up where it was. The music ducks with the effects under speech.
  Music Volume 10 = half the CD input. Which map gets which track is one list in `tools/mkmusic.py`.
  Rebuild: `tools/musicprep.sh <cube dir> <pcm dir>`, then `tools/mkmusic.py <pcm dir> maps/music.xa` (the source zip
  leaves out the 116 MB `music.xa`).

* **Map props** (Cube's mapmodels; `tools/mkmodel.py`, `mktex.py`, `build_mapmodels` in `src/world.c`): the 9 of Cube's
  default mapmodel slots our maps use - tree1 and bridge, and DCP's tree2, tree3, palmtree, thorns, plant1, grass and
  ivy - on cruel02 (2) and the WSG maps (24 / 56 / 41). Static models (24-120 triangles, a lower-detail copy for
  distance), placed as Cube does: at the entity's lattice point, on the floor + the slot's z offset + attr3, turned to
  attr1 snapped to 15 degrees, lit by the square they stand on. Their skins travel in the texture pack of each map that
  uses them (slots 240+), so they cost no video memory anywhere else; on wsg2 that leaves room for 15 full-colour
  textures instead of 23. Collision is Cube's mmcollide: a box of the slot's radius from its base to its height - from
  below it is a ceiling, from above a floor, and taller than a step it blocks (tree trunks stop you; bridges carry you).
  Grass and ivy (height 0) do not collide, as in Cube. DCP's readme asks to be told about uses outside Cube.

Not done yet: dynamic lights, Cube's fog colour.

## Building
```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<psn00bsdk>/lib/libpsn00b/cmake/sdk.cmake -DPSN00BSDK_TARGET=mipsel-linux-gnu
cmake --build build          # build/cubepsx.bin + .cue
python3 tools/checkops.py build/cubepsx.elf      # no instruction the R3000 lacks
```
Assets (paths inside the tools point at an unpacked Cube release):
`tools/cgz2cmp.py map.cgz maps/x.cmp`, `tools/mktex.py map.cgz maps/x.tex`, `tools/mkmodel.py maps/models.pak`,
`tools/mksky.py`.

## Verification
`rig/` runs the real `cubepsx.elf` on an emulated R3000 (Unicorn, `pip install unicorn`) with a modelled GTE,
stand-ins for the SDK's hardware calls and a software GPU that follows the hardware's polygon rules:
* `rig/bench.py elf` - instructions per frame over random poses (564,480 = one vblank; calibrated against a real
  emulator's FPS counter at 1 cycle per instruction).
* `rig/walk.py elf map` - a timed tour with the real frame pacing and adaptive view distance (all doors opened first).
* `rig/prof.py elf map x y yaw pitch` - per-function instruction profile at one pose; `rig/profline.py elf func ...` per line.
* `rig/holes.py elf` - pre-fills the view with magenta and reports pixels nothing drew, and polygons the GPU would drop.
* `rig/verify.py elf` - compares against a brute-force reference (no occlusion culling / no merging).
* `test/host_test.c`, `trig_test.c`, `demo_test.c` - game rules on every map, doors, teleports, jump pads, level end and
  the demo route under ASan/UBSan on the host.
* `test/save_test.c` - profiles: creation, the unlock chain over unported maps, best times, options, a saved game restored.
* `test/link_test.c` - link play: two players in one game, the same game from either console.
* `test/sound_test.c` - voice rules (quip rate and no repeats over 300 kills, grunt spacing, one complaint per door), and sound events: firing, switching, jumping, a shotgun blast as one pain sound, death sounds,
  every pickup, door and teleport on every map, the splash at pig09's spawn.
* `rig/audio.py elf out.wav` - boots, lets the attract demo play and mixes what the sound chip was told to play
  (`rig/spu.py` decodes the ADPCM in sound RAM) into a WAV.
* `test/water_test.c` - water on pig09: sinking speed, swimming where you look, and climbing out of the canal.
* `test/ai_test.c` - monster AI on a real map: waking by angle and distance, dodging by side-stepping, pain, infighting,
  walking round walls, bodies blocking, and a damage-per-minute table per monster and difficulty.
* `rig/frontend.py elf out.png` - the front end from power-on through the pad, with a stand-in memory card: options pages,
  a new profile (keyboard, difficulty), the map selector, pause and save, finishing a map, loading the profile.
* `rig/face.py elf out.png` - the portrait in the real status bar through idle, damage, hit, kill and death.

The rig is not a console: it does not model the CPU cache, memory speed or GPU fill time, so real hardware will be
slower than its numbers.
