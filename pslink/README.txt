PSLink 1.0 - two PlayStations on a link cable, in one window
=============================================================

PSLink runs two PlayStation consoles side by side and joins their serial ports with an emulated link cable, so a
link-cable game can be played by two people on one PC. Made for Cube PSX's link play (v0.22.0 and later); any game
that uses the PS1 link cable should work.

Starting
  Run pslink.exe and choose the disc (a .cue file). Both consoles boot it. Or drop a .cue file on pslink.exe, or on
  the PSLink window. PSLink remembers the last disc (pslink.ini).
  To give console 2 a different disc: pslink.exe disc1.cue disc2.cue
  No BIOS file is needed (a built-in one is used). To use a real one, put it (e.g. scph1001.bin) in the "system"
  folder and set bios = auto in pslink.ini.

Controllers
  The first controller plays console 1 (left screen), the second console 2 (right). Xbox-style pads and most others
  work; sticks are the PlayStation's analog sticks. A = Cross, B = Circle, X = Square, Y = Triangle, LB/RB = L1/R1,
  LT/RT = L2/R2, Back = Select, Start = Start.

Keyboard (both work at once; change them in pslink.ini)
  Console 1: W A S D move, J L turn, I K look, Space = Cross (jump), F = R2 (fire), G = L2, Q / E = L1 / R1,
             R = Triangle, T = Square, C = Circle, Enter = Start, Tab = Select
  Console 2: arrow keys move, number pad 4 6 turn, 8 5 look, pad 0 = Cross, Right Ctrl = R2, pad 3 = L2,
             pad 7 / 9 = L1 / R1, pad . = Triangle, pad + = Square, pad 1 = Circle, pad Enter = Start, pad - = Select

Keys
  F1 help   F2 reset both consoles   F4 swap controllers between the consoles   F5 sound: both / console 1 / console 2
  F6 side by side / one above the other   F7 unplug / plug in the link cable   F9 pause   F11 full screen

Cube PSX link play
  On both screens: Multiplayer > Link Cable. When each says OTHER CONSOLE FOUND, either player sets up the game and
  presses START; the other joins. In the warm-up each player presses fire when ready.

Memory cards are kept per console in saves\console1 and saves\console2. A log is written to pslink.log.

PSLink is built on PCSX-ReARMed (libretro), with the serial port added; both are GPL v2 (COPYING.txt; the source and
how to build it are in the "source" folder). Two copies of the emulator run in their own memory: console 2's core is
copied from cores\pslink_core.dll at start-up. It needs Windows 10 or 11 (64-bit).
