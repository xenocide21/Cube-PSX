# ---------- 1 cover ----------
cover=f'''<img src="{U('space')}" alt="Space above Facing Cubes" style="position: absolute; left: 0; top: 0; width: {P}px; height: {P}px; object-fit: cover; opacity: 0.55">
<div style="position: absolute; left: 0; top: 0; width: {P}px; height: {P}px; background: linear-gradient(180deg, rgba(10,10,14,0.15) 0%, rgba(10,10,14,0.55) 55%, rgba(10,10,14,0.96) 100%)"></div>
<div style="position: absolute; left: 0; top: 0; width: 30px; height: {P}px; background: #000"></div>
<div style="position: absolute; left: 0; top: 0; width: 30px; height: {P}px; display: flex; align-items: center; justify-content: center"><div style="transform: rotate(-90deg); white-space: nowrap; font-family: 'Chakra Petch', sans-serif; font-weight: 700; font-size: 13px; letter-spacing: 0.3em; color: {GOLDL}">CUBE PSX</div></div>
<div style="position: absolute; left: 54px; top: 36px; width: 360px; display: flex; align-items: center; gap: 14px">
<img src="{U('logo')}" alt="The Cube logo" style="flex: 0 0 auto; width: 64px; height: 64px; image-rendering: pixelated">
<div style="flex: 1 1 auto"><div style="font-family: 'Chakra Petch', sans-serif; font-weight: 700; font-size: 46px; line-height: 1; color: #fff; letter-spacing: 0.02em">CUBE PSX</div>
<div style="font-family: 'Chakra Petch', sans-serif; font-weight: 600; font-size: 12px; letter-spacing: 0.14em; color: {GOLDL}; margin-top: 4px">THE PLAYSTATION PORT OF CUBE</div></div>
</div>
<img src="{U('chad')}" alt="Giga Chad" style="position: absolute; left: 64px; top: 150px; width: 150px; height: 184px; image-rendering: pixelated">
<img src="{U('stacy')}" alt="Giga Stacy" style="position: absolute; left: 244px; top: 150px; width: 150px; height: 184px; image-rendering: pixelated">
<div style="position: absolute; left: 54px; top: 356px; width: 240px; font-family: 'Chakra Petch', sans-serif; font-weight: 700; font-size: 15px; color: #fff; line-height: 1.25">27 MAPS · BOTS · LINK CABLE</div>
<div style="position: absolute; left: 54px; top: 378px; width: 240px; font-family: 'Chakra Petch', sans-serif; font-weight: 600; font-size: 12px; letter-spacing: 0.06em; color: {GOLDL}; line-height: 1.3">1–2 PLAYERS · MEMORY CARD · ANALOG</div>
<div style="position: absolute; left: 300px; top: 386px; width: 130px; font-size: 12px; color: #C9C4B8; text-align: right; line-height: 1.3">Fan-made homebrew<br>Not licensed by Sony</div>'''
add('Main.dc.html','Cover', cover, DARK)

# ---------- 2 contents ----------
toc=[('Getting started',3),('Controls',4),('The screen',6),('Starting out',7),('Your character',8),('Single player',9),('XENO',10),('Weapons',11),('Items',12),('Monsters',13),('The automap',14),('Pause, saving, options',15),('Quickplay',16),('Warm-up, Facing Cubes',17),('Flags and hills',18),('Link cable play',19),('Tips and tricks',20),('Glossary',21),('Credits',22),('License',24),('Index',27)]
def tocol(xs):
    out=''.join(f'<div style="display: flex; align-items: baseline; gap: 6px; font-size: 12px; line-height: 1.6"><span>{esc(a)}</span><span style="flex: 1 1 auto; border-bottom: 1px dotted #9C968A"></span><span style="font-weight: 700">{b}</span></div>' for a,b in xs)
    return Blk(f'<div style="{NS}; width: 179px; display: flex; flex-direction: column">{out}</div>', len(xs)*12*1.6)
half=(len(toc)+1)//2
add('Page02.dc.html','Contents', inner_page(2,'Contents', [cols(tocol(toc[:half]), tocol(toc[half:]))]))

# ---------- 3 getting started ----------
add('Page03.dc.html','Getting started', inner_page(3,'Getting started', [
 para('Put the Cube PSX disc in your PlayStation, or open <b>cubepsx.cue</b> in an emulator, and switch on.'),
 head3('Memory card'),
 para('A memory card in slot 1 keeps your profile: your name, character and options, the maps you have beaten and your best times, and one saved game. A profile takes 2 blocks, and there are three profile slots.'),
 para('No card? You can still play. Everything works until you switch off.'),
 head3('Controller'),
 para('An analog controller is best. A digital pad works too: the D-pad moves and turns.')], 'Before you play'))

# ---------- 4-5 controls ----------
def badge(n, x, y): return f'<div style="position: absolute; left: {x}px; top: {y}px; width: 20px; height: 20px; border-radius: 50%; background: {GOLD}; color: #fff; font-size: 12px; font-weight: 700; display: flex; align-items: center; justify-content: center">{n}</div>'
def fbtn(sym, color, x, y): return f'<div style="position: absolute; left: {x}px; top: {y}px; width: 24px; height: 24px; border-radius: 50%; background: #2A2830; color: {color}; font-size: 13px; font-weight: 700; display: flex; align-items: center; justify-content: center">{sym}</div>'
padhtml=f'''<div style="{NS}; position: relative; width: 374px; height: 172px">
<div style="position: absolute; left: 62px; top: 22px; width: 62px; height: 14px; border-radius: 6px 6px 0 0; background: #2A2830"></div>
<div style="position: absolute; left: 250px; top: 22px; width: 62px; height: 14px; border-radius: 6px 6px 0 0; background: #2A2830"></div>
<div style="position: absolute; left: 40px; top: 34px; width: 294px; height: 96px; border-radius: 48px; background: #4A4650"></div>
<div style="position: absolute; left: 30px; top: 80px; width: 90px; height: 84px; border-radius: 45px; background: #4A4650"></div>
<div style="position: absolute; left: 254px; top: 80px; width: 90px; height: 84px; border-radius: 45px; background: #4A4650"></div>
<div style="position: absolute; left: 72px; top: 56px; width: 16px; height: 48px; background: #1D1C21"></div>
<div style="position: absolute; left: 56px; top: 72px; width: 48px; height: 16px; background: #1D1C21"></div>
{fbtn('△','#7FD1B0',276,48)}{fbtn('□','#E79AC6',252,70)}{fbtn('○','#F07A6A',300,70)}{fbtn('✕','#8EB6F0',276,92)}
<div style="position: absolute; left: 136px; top: 104px; width: 34px; height: 34px; border-radius: 50%; background: #1D1C21; border: 3px solid #6A6670"></div>
<div style="position: absolute; left: 204px; top: 104px; width: 34px; height: 34px; border-radius: 50%; background: #1D1C21; border: 3px solid #6A6670"></div>
<div style="position: absolute; left: 152px; top: 74px; width: 24px; height: 9px; border-radius: 4px; background: #1D1C21"></div>
<div style="position: absolute; left: 198px; top: 74px; width: 24px; height: 9px; border-radius: 4px; background: #1D1C21"></div>
{badge(1,82,0)}{badge(2,272,0)}{badge(3,28,52)}{badge(4,112,140)}{badge(5,242,140)}{badge(6,330,44)}{badge(7,330,96)}{badge(8,154,50)}{badge(9,200,50)}
</div>'''
pad=Blk(padhtml, 172)
c1=[('1 · L1 / L2','L1 strafes left · L2 fires'),('2 · R1 / R2','R1 strafes right · R2 fires'),('3 · D-pad','Up / down move · left / right turn'),('4 · Left stick','Move and strafe'),('5 · Right stick','Turn, look up and down')]
add('Page04.dc.html','Controls', inner_page(4,'Controls', [pad, table(c1,112)], 'Analog or digital'))
c2=[('6 · △ / □','Next / previous weapon'),('7 · ✕ / ○','✕ jumps · ○ does nothing in play'),('8 · SELECT','Automap on / off'),('9 · START','Pause (single player) or the match menu (multiplayer) · back in after being fragged')]
add('Page05.dc.html','Controls, continued', inner_page(5,'Controls', [table(c2,112), head3('In menus'), para('✕ chooses, ○ goes back, the D-pad or left stick moves the cursor, and left / right change a setting.'), head3('In the warm-up'), para('You fly: go where you look, ✕ to rise. Fire (R2 or L2) starts the match.')], 'Continued'))

# ---------- 6 the screen ----------
add('Page06.dc.html','The screen', inner_page(6,'The screen', [shots(('hud','The game screen and status bar')),
 table([('Portrait','Your character: more battered as your health drops'),('Health','100 is full; a Boost takes it to 200'),('Armour','Soaks up part of each hit'),('Weapon, ammo','The gun in your hands, the rounds it has'),('Kills','Monsters killed / monsters on the map')],92)], 'The status bar'))

# ---------- 7 starting out ----------
add('Page07.dc.html','Starting out', inner_page(7,'Starting out', [shots(('title','The title menu'),('hub','The hub')),
 para('<b>New Game</b>: pick a memory card slot, type a name (START when done), choose your character, then a difficulty:'),
 table([('Easy','Monsters hit softer, think slower'),('Normal','Cube as it was meant to be played'),('Hard','Faster, meaner, harder hitting')],70),
 para('<b>Load Game</b> picks up a profile. Both lead to the <b>hub</b>.')], 'Menus and profiles'))

# ---------- 8 character ----------
def port(k, name):
    return f'<div style="flex: 0 0 auto; display: flex; flex-direction: column; gap: 6px; align-items: center"><img src="{U(k)}" alt="{name}" style="width: 120px; height: 147px; image-rendering: pixelated; background: #2A2830; border: 1px solid {INK}"><div style="font-family: \'Chakra Petch\', sans-serif; font-weight: 700; font-size: 14px">{name}</div></div>'
ports=Blk(f'<div style="{NS}; display: flex; gap: 24px; justify-content: center">{port("chad","GIGA CHAD")}{port("stacy","GIGA STACY")}</div>', 147+6+20)
add('Page08.dc.html','Your character', inner_page(8,'Your character', [ports,
 para('Choose when you make a profile; change it any time from <b>Character</b> in the hub. It is your face on the status bar and your voice: the king of the arena, or its queen.')], 'Giga Chad or Giga Stacy'))

# ---------- 9 single player ----------
add('Page09.dc.html','Single player', inner_page(9,'Single player', [shots(('camp','The campaign list'),('cruel01','Cruel Part I')),
 table([('PIGCAM','10 maps · Jonathan Whiting'),('CRUEL','2 maps · unicious'),('WHY SO GREEN','3 maps · SirLiveAlot'),('XENO','8 maps · Xenocide21')],110),
 para('Beat a map to open the next. Reach the exit to win; kill every monster too for a clean sweep.')], 'Four campaigns, 23 maps'))

# ---------- 10 XENO ----------
add('Page10.dc.html','XENO', inner_page(10,'XENO', [shots(('xeno','Intake, the first XENO map')),
 cols(table([('xeno01','Intake'),('xeno02','Flooded Works'),('xeno03','Night Courtyard'),('xeno04','The Vaults')],56,179), table([('xeno05','Cisterns'),('xeno06','Gauntlet'),('xeno07','Starfall Keep'),('xeno08','Xenocide')],56,179))], 'The PlayStation campaign'))

# ---------- 11 weapons ----------
add('Page11.dc.html','Weapons', inner_page(11,'Weapons', [table([('Fist','Up close, never runs out · 50 damage'),('Pistol','A shot each half second · 22 damage'),('Shotgun','A spread of pellets: deadly up close'),('Chaingun','Rapid fire · 30 a bullet'),('Rockets','120 and a blast: mind your feet'),('Rifle','Slow, precise · 100 at any range')],80),
 para('You start with the fist and the pistol (20 rounds). △ and □ go through the guns you carry; when one runs dry, the next comes up.')], 'Six guns'))

# ---------- 12 items ----------
add('Page12.dc.html','Items', inner_page(12,'Items', [table([('Health','+25, up to 100'),('Boost','+50, up to 200'),('Green armour','100 armour'),('Yellow armour','150 armour, takes more of each hit'),('Ammo','Shells, bullets, rockets, rounds, pistol boxes'),('Carrot','A switch: opens doors, starts traps, ends maps'),('Teleporter','Takes you to its destination'),('Jump pad','Throws you up or across')],100)], 'What to pick up'))

# ---------- 13 monsters ----------
add('Page13.dc.html','Monsters', inner_page(13,'Monsters', [table([('Ogro','Fireballs · 100 health'),('Rhino','Chaingun · quick · 70'),('Ratamahatta','Shotgun · 120'),('Slith','Rifle, from afar · 200'),('Bauul','Rockets · 500'),('Hellpig','Bites · the fastest · 50'),('Knight','Iceballs · slow, tough · 250'),('Goblin','Slimeballs · 100')],100),
 para('Monsters that hit each other start fighting each other: use it.')], 'Who wants you dead'))

# ---------- 14 automap ----------
add('Page14.dc.html','The automap', inner_page(14,'The automap', [shots(('automap','The automap')),
 para('SELECT shows everything you have seen, from above, north up. Red: walls · brown: drops · the arrow: you · magenta: the exit · cyan: teleporters. The game goes on while it is up.')], 'Finding your way'))

# ---------- 15 pause ----------
add('Page15.dc.html','Pause, saving, options', inner_page(15,'Pause and saving', [shots(('pause','The pause menu'),('quick','The Quickplay screen')),
 para('<b>START</b> pauses: Resume, Save Game, Options, Quit to Map Select, Quit to Title. Save Game keeps one game in your profile; choose its map again to carry on (✕) or start over (□).'),
 para('<b>Options</b>: move, turn and look speed, invert look; sound, speech and music volume; aim assist.')], 'Saving and options'))

# ---------- 16 quickplay ----------
add('Page16.dc.html','Quickplay', inner_page(16,'Quickplay', [table([('Deathmatch','Everyone for themselves · 15 frags'),('Team DM','Red against blue · 20 frags'),('Capture the Flag','Their flag to your base · 3'),('King of the Hill','Hold the hill alone · 60 points')],110),
 para('You against 1 to 7 bots, at Easy, Normal or Hard, on Facing Cubes or the multiplayer maps. In team games you are red. A fall into space, or your own rocket, costs a frag.')], 'You against the bots'))

# ---------- 17 warm-up ----------
add('Page17.dc.html','Warm-up and Facing Cubes', inner_page(17,'Warm-up', [shots(('space','Space above Facing Cubes'),('ctfbase','A temple hall')),
 para('When the map has loaded it is yours alone: fly anywhere. Fire when ready, and you and every bot come in at once.'),
 para('<b>Facing Cubes</b>: two temples on islands in space, long bridges, a middle with cover. Rifles on the roofs; teleporters inside go up.')], 'And Facing Cubes'))

# ---------- 18 flags and hills ----------
add('Page18.dc.html','Flags and hills', inner_page(18,'Flags and hills', [shots(('ctfflag','The red flag at home'),('match','The match menu')),
 para('<b>CTF</b>: take their flag, and touch yours at home to score. A dropped flag goes home when its team touches it, or after 30 s.'),
 para('<b>King of the Hill</b>: a point a second while only your team stands on it. <b>START</b> shows the score table.')], 'Team play'))

# ---------- 19 link ----------
add('Page19.dc.html','Link cable play', inner_page(19,'Link cable play', [Blk(f'<img src="{U("link")}" alt="Two consoles linked" style="{NS}; width: 374px; height: 140px; object-fit: cover; display: block; border: 1px solid {INK}">',142),
 para('Both players: <b>Multiplayer › Link Cable</b>. When it says <b>OTHER CONSOLE FOUND</b>, either picks mode, map, bots, teams and skill, and presses START.'),
 para('Fire when ready. <b>Quit Match</b> gives the other player the win. On a PC, <b>PSLink</b> runs two linked consoles.')], 'Two players'))

# ---------- 20 tips ----------
tips=['Keep moving: a moving target is missed far more.','Let monsters fight each other.','Rockets hurt you too: not at a wall you stand by.','The rifle rewards one careful shot.','Lost? SELECT shows where you have not been.','Carrots can be traps: step back after one.','Bots see about as far as you: use the fog.','In CTF, take armour before the long climb.','In the warm-up, find the rockets first.']
tb=Blk('<div style="flex: 0 0 auto; display: flex; flex-direction: column; gap: 6px">'+''.join(f'<div style="display: flex; gap: 8px; font-size: 12px; line-height: 1.4"><span style="flex: 0 0 auto; font-family: \'Chakra Petch\', sans-serif; font-weight: 700; color: {GOLD}">{i+1:02d}</span><span>{esc(t)}</span></div>' for i,t in enumerate(tips))+'</div>', len(tips)*(12*1.4*1)+6*(len(tips)-1))
add('Page20.dc.html','Tips and tricks', inner_page(20,'Tips and tricks', [tb], 'From the arena'))

# ---------- 21 glossary ----------
g=[('Aim assist','Bends shots toward a monster'),('Automap','Map of what you have seen'),('Bot','A computer player'),('Carrot','A trigger on a stick'),('CTF','Capture the Flag'),('Frag','A kill in a match'),('KOTH','King of the Hill'),('Link cable','Joins two consoles'),('Profile','Your save on the card'),('PSLink','Two linked consoles on a PC'),('TDM','Team Deathmatch'),('Warm-up','Flying round before a match')]
add('Page21.dc.html','Glossary', inner_page(21,'Glossary', [table(g,84)], 'Words in this manual'))

# ---------- 22-23 credits ----------
cr1=[('Cube','Wouter van Oortmerssen (Aardappel), 2001–2005'),('Cube PSX','Xenocide21, 2026 · programmed with Claude (Anthropic)'),('Maps','PIGCAM: Jonathan “Piglet” Whiting · CRUEL: unicious · WHY SO GREEN: SirLiveAlot · XENO, Facing Cubes: Xenocide21'),('Voices','Xenocide21'),('Textures','Iikka “Fingers” Keränen · Sock · Remedy Entertainment (Max Payne textures) · Than · John Fitzgibbons · Schwenz · Aardappel · Crack dot Com (Golgotha, public domain) · the dg set from Cube')]
cr2=[('Music','Marc A. Pullen (Fanatic): The Cube Album, “Revenge” · DaMaul: cubez0r · Lu-Wang: cube1, ambient, deathB (TimGM6mb soundfont, Tim Brechbill, GPL v2)'),('Monsters','Magarnigal, Ogro_Fix · Andreas Möller, Andrew Eglington, Jens Matthies · Brian Collins · Ryan Butts, Jeramy Cooke · James Green · Conrad · Psionic'),('Guns, armour','DarthVim · Seth and Ulrich Galbraith (GPL v2)'),('Map models','dcp (Dietmar Pier)'),('Sounds','Aardappel · Awesund (SiN) · DarthVim · Quake II models'),('Tools','PSn00bSDK · PCSX-ReARMed')]
add('Page22.dc.html','Credits 1', inner_page(22,'Credits', [table(cr1,72)], 'The people behind it'))
add('Page23.dc.html','Credits 2', inner_page(23,'Credits', [table(cr2,72), para('QUAKE® and QUAKE II®: trademarks of id Software.', color=MUTED)], 'Continued'))

# ---------- 24-26 license ----------
notice=['Cube game engine source code, 20 dec 2003 release. Copyright (C) 2001-2005 Wouter van Oortmerssen.',"This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.",'Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:','1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.','2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.','3. This notice may not be removed or altered from any source distribution.','4. Source versions may not be "relicensed" under a different license without my explicitly written permission.']
def nbox(xs):
    hh=sum(nlines(len(x),354)*12*1.35 for x in xs)+4*(len(xs)-1)+16
    return Blk(f'<div style="{NS}; display: flex; flex-direction: column; gap: 4px; border: 1px solid #C9C2B2; padding: 7px 9px; background: #FBF9F4">'+''.join(f'<p style="font-size: 12px; line-height: 1.35; margin: 0">{esc(x)}</p>' for x in xs)+'</div>', hh)
add('Page24.dc.html','License 1', inner_page(24,'License', [para('The program is an altered version of Cube’s source code, not the original. Cube’s license, unaltered:'), nbox(notice[:3])], 'Cube’s license'))
add('Page25.dc.html','License 2', inner_page(25,'License', [nbox(notice[3:]), para('The changes, © 2026 Xenocide21, are under the same terms.')], 'Cube’s license, continued'))
add('Page26.dc.html','License 3', inner_page(26,'License', [para('The maps, art, sound and music keep their authors’ terms. Together they allow:'),
 table([('Free only','Never sold or paid for'),('Download','No pressed or sold discs; burn your own'),('Credit','The credits go with every copy')],80),
 para('XENO, Facing Cubes, the portraits and voices © 2026 Xenocide21: free to share, non-commercially. Full texts: LICENSE.txt.'),
 para('Not affiliated with or endorsed by Sony Interactive Entertainment. PlayStation is a trademark of Sony Interactive Entertainment Inc. No warranty.', color=MUTED)], 'The game media'))

# ---------- 27 index ----------
idx=[('Aim assist','15'),('Armour','12'),('Automap','14'),('Bots','16'),('Carrot','12'),('Character','8'),('Controls','4'),('Credits','22'),('CTF','16, 18'),('Deathmatch','16'),('Difficulty','7'),('Facing Cubes','17'),('Health','6, 12'),('Hub','7'),('Items','12'),('King of the Hill','16, 18'),('License','24'),('Link cable','19'),('Match menu','18'),('Memory card','3'),('Monsters','13'),('Options','15'),('Pause','15'),('Profile','3, 7'),('PSLink','19'),('Quickplay','16'),('Saving','15'),('Status bar','6'),('Tips','20'),('Warm-up','17'),('Weapons','11'),('XENO','10')]
def icol(xs): return Blk(f'<div style="{NS}; width: 114px; display: flex; flex-direction: column">'+''.join(f'<div style="display: flex; justify-content: space-between; gap: 4px; font-size: 12px; line-height: 1.5"><span>{esc(a)}</span><span style="font-weight: 700">{b}</span></div>' for a,b in xs)+'</div>', len(xs)*18)
t3=(len(idx)+2)//3
add('Page27.dc.html','Index', inner_page(27,'Index', [cols(icol(idx[:t3]),icol(idx[t3:2*t3]),icol(idx[2*t3:]), gap=16)]))

# ---------- 28 back page ----------
back=f'''<div style="position: absolute; left: 0; top: 0; width: 30px; height: {P}px; background: #000"></div>
<img src="{U('logo')}" alt="The Cube logo" style="position: absolute; left: 60px; top: 50px; width: 56px; height: 56px; image-rendering: pixelated">
<div style="position: absolute; left: 60px; top: 124px; width: 354px; font-family: 'Chakra Petch', sans-serif; font-weight: 700; font-size: 26px; color: #fff">Thanks for playing.</div>
<p style="position: absolute; left: 60px; top: 166px; width: 354px; font-size: 13px; line-height: 1.5; margin: 0; color: #E8E3D8">Cube was made by Wouter van Oortmerssen as a game anyone could open up and change. Cube PSX carries it, and the maps, art and music its community made, onto the PlayStation.</p>
<p style="position: absolute; left: 60px; top: 262px; width: 354px; font-size: 13px; margin: 0; color: {GOLDL}">Cube: cubeengine.com</p>
<p style="position: absolute; left: 60px; top: 360px; width: 354px; font-size: 12px; line-height: 1.4; margin: 0; color: #B9B3A6">Cube PSX v0.23 · Free fan-made homebrew, not for sale. Not licensed by or affiliated with Sony Interactive Entertainment.</p>'''
add('Page28.dc.html','Back page', back, DARK)
for n in sorted(heights): print(n, round(heights[n]))
