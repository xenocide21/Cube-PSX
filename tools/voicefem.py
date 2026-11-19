#!/usr/bin/env python3
"""voicefem.py - Giga Stacy's voice: the dude's lines (assets/voice) tuned into a deep female voice (assets/voice_f).

Two passes of rubberband: first pitch and formants up together by 19% (the vocal tract of a smaller speaker), then the
pitch alone up a further 12% with the formants kept.  About +5 semitones in all, formants +19%: low for a woman, still
plainly one.  The timing is untouched, so every line (and the holy-shit echo) keeps its length.

usage: voicefem.py [--pitch1 1.19] [--pitch2 1.12]"""
import os, subprocess, sys, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'assets', 'voice'); DST = os.path.join(HERE, '..', 'assets', 'voice_f')
ap = argparse.ArgumentParser(); ap.add_argument('--pitch1', type=float, default=1.19); ap.add_argument('--pitch2', type=float, default=1.12)
a = ap.parse_args()
os.makedirs(DST, exist_ok=True)
for f in sorted(os.listdir(SRC)):
    if not f.endswith('.wav'): continue
    af = ('rubberband=pitch=%.4f:formant=shifted:pitchq=quality,rubberband=pitch=%.4f:formant=preserved:pitchq=quality,'
          'alimiter=limit=0.89') % (a.pitch1, a.pitch2)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(SRC, f), '-af', af, os.path.join(DST, f)], check=True)
    print(f)
