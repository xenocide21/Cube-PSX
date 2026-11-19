#!/bin/sh
# Cube's music -> 37.8 kHz stereo 16-bit raw, loudness matched.  usage: musicprep.sh <cube dir> <out dir>
# MIDI songs are rendered with tools/midi2wav.py (TinySoundFont, General MIDI soundfont); cubez0r.xm by ffmpeg's libopenmpt.
P="$1/packages"; O="$2"; T=$(dirname "$0"); mkdir -p "$O"
SF=${SF:-/usr/share/sounds/sf2/TimGM6mb.sf2}
for m in cube1 ambient deathB; do python3 "$T/midi2wav.py" "$P/songs/$m.mid" "$O/$m.wav" "$SF"; done
for f in "$P"/fanatic/*.ogg "$P/songs/cubez0r.xm" "$O"/cube1.wav "$O"/ambient.wav "$O"/deathB.wav; do
	b=$(basename "${f%.*}")
	ffmpeg -hide_banner -loglevel error -y -i "$f" -af "loudnorm=I=-17:TP=-1.5:LRA=11,aresample=37800" -ac 2 -ar 37800 -f s16le "$O/$b.raw"
done
