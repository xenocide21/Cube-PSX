#!/usr/bin/env python3
"""Cut, clean, deepen and level the voice lines from the user's recording; writes voice/<name>.wav (22.05 kHz mono)
and a preview of them all in a row."""
import subprocess, wave, numpy as np, os

SR = 22050
src = 'rec.wav'
w = wave.open(src); X = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float); assert w.getframerate() == SR
win = int(SR * 0.02); E = np.sqrt(np.convolve(X * X, np.ones(win) / win, 'same'))

def cut(a, b):
    """from a to b seconds, grown outward until the sound has really died away (quiet word endings like 'here')"""
    i, j = int(a * SR), int(b * SR)
    while i > 0 and E[i] > 25: i -= 1
    k = j
    while k < len(E) - 1 and (E[k] > 25 or np.any(E[k:k + int(0.15 * SR)] > 60)): k += 1
    i = max(0, i - int(0.03 * SR)); k = min(len(X), k + int(0.08 * SR))
    return X[i:k]

def shorten_gap(x, max_gap=0.3):
    """'Locked. ... Of course it's locked.' - keep the beat but not the whole pause"""
    e = np.sqrt(np.convolve(x * x, np.ones(win) / win, 'same')); q = e < 40
    out = []; i = 0; n = len(x); g = int(max_gap * SR)
    while i < n:
        if q[i]:
            j = i
            while j < n and q[j]: j += 1
            out.append(x[i:i + min(j - i, g)]); i = j
        else:
            j = i
            while j < n and not q[j]: j += 1
            out.append(x[i:j]); i = j
    return np.concatenate(out)

def fx(x, name, pitch, formant='shifted', extra=''):
    tmp_in, tmp_out = '/tmp/v_in.wav', 'out/%s.wav' % name
    ww = wave.open(tmp_in, 'wb'); ww.setnchannels(1); ww.setsampwidth(2); ww.setframerate(SR)
    ww.writeframes(np.clip(x, -32767, 32767).astype(np.int16).tobytes()); ww.close()
    # level it like broadcast speech: even out the words within the line (dynaudnorm), squeeze it (compressor), cap
    # the peaks (limiter) - a line with one loud syllable ("PIE-ce of cake") must not end up mostly quiet
    chain = ('highpass=f=70,afftdn=nf=-40,rubberband=pitch=%.4f:formant=%s,'
             'dynaudnorm=f=60:g=5:m=30:p=0.9:s=6,'
             'acompressor=threshold=0.06:ratio=6:attack=3:release=80:makeup=4,'
             'agate=threshold=0.02:ratio=4:attack=2:release=60,'
             'alimiter=limit=0.89:attack=2:release=40:level=disabled%s' % (pitch, formant, extra))
    subprocess.run(['ffmpeg', '-v', 'quiet', '-y', '-i', tmp_in, '-af', chain, '-ar', str(SR), '-ac', '1', '/tmp/v_mid.wav'], check=True)
    ww = wave.open('/tmp/v_mid.wav'); y = np.frombuffer(ww.readframes(ww.getnframes()), np.int16).astype(float)
    y = y / max(1.0, np.max(np.abs(y))) * 32767 * 0.89                     # -1 dB peak
    fade = int(0.04 * SR); y[-fade:] *= np.linspace(1, 0, fade)
    return y

def save(y, name):
    ww = wave.open('out/%s.wav' % name, 'wb'); ww.setnchannels(1); ww.setsampwidth(2); ww.setframerate(SR)
    ww.writeframes(np.clip(y, -32767, 32767).astype(np.int16).tobytes()); ww.close()

def drag_sh(y, start, stretch=1.3):
    """stretch the hiss of "shit" (loud, noisy: many zero crossings) that follows `start`; returns y and the samples added
    before the word's vowel"""
    z = np.convolve((np.diff(np.sign(y), prepend=0) != 0).astype(float), np.ones(win) / win, 'same')
    e = np.sqrt(np.convolve(y * y, np.ones(win) / win, 'same'))
    hiss = (z > 0.2) & (e > 1000)
    i = start
    while i < len(y) and not hiss[i]: i += 1
    j = i
    while j < len(y) and (hiss[j] or np.any(hiss[j:j + win])): j += 1
    if j - i < int(0.05 * SR): return y, 0
    seg = '/tmp/v_sh.wav'; ww = wave.open(seg, 'wb'); ww.setnchannels(1); ww.setsampwidth(2); ww.setframerate(SR)
    ww.writeframes(np.clip(y[i:j], -32767, 32767).astype(np.int16).tobytes()); ww.close()
    subprocess.run(['ffmpeg', '-v', 'quiet', '-y', '-i', seg, '-af', 'rubberband=tempo=%.4f' % (1 / stretch), '-ar', str(SR), '/tmp/v_sh2.wav'], check=True)
    ww = wave.open('/tmp/v_sh2.wav'); s2 = np.frombuffer(ww.readframes(ww.getnframes()), np.int16).astype(float)
    k = int(0.006 * SR)                                                     # short crossfades either side
    s2[:k] = s2[:k] * np.linspace(0, 1, k) + y[i:i + k] * np.linspace(1, 0, k)
    s2[-k:] = s2[-k:] * np.linspace(1, 0, k) + y[j - k:j] * np.linspace(0, 1, k)
    return np.concatenate([y[:i], s2, y[j:]]), len(s2) - (j - i)

def echo_tail(y, delays=(0.60, 1.32, 2.18), gains=(0.45, 0.24, 0.11)):
    """the shit.. shit.. shit: the last word again from where it began, each repeat later and quieter, mixed in like a
    real echo (a repeat may start while the one before is still dying away) - about 3.5 s in all, as UT99's"""
    e = np.sqrt(np.convolve(y * y, np.ones(win) / win, 'same'))
    mid = slice(int(len(y) * 0.3), int(len(y) * 0.75))
    dip = mid.start + int(np.argmin(e[mid]))                                # the gap between "holy" and "shit"
    tail = y[dip:]
    tail = tail[:int(np.nonzero(np.abs(tail) > 600)[0][-1]) + int(0.05 * SR)] if np.any(np.abs(tail) > 600) else tail
    y, grow = drag_sh(y, dip)                                               # the first "sh" a little longer
    dip += grow
    out = np.zeros(dip + int(delays[-1] * SR) + len(tail) + int(0.1 * SR)); out[:len(y)] += y
    for d, g in zip(delays, gains):
        a = dip + int(d * SR); out[a:a + len(tail)] += tail * g
    return out / max(1.0, np.max(np.abs(out))) * 32767 * 0.89

os.makedirs('out', exist_ok=True)
DUDE, ANN = 0.84, 0.79           # -3 and -4 semitones
lines = [                          # name, clip(s) (start, end seconds), voice
    ('grunt1', [(1.69, 1.85)], DUDE), ('grunt2', [(13.33, 13.49)], DUDE), ('die', [(17.20, 18.95)], DUDE),
    ('ohyeah', [(25.72, 26.63)], DUDE), ('easy', [(29.49, 30.51)], DUDE), ('hurt', [(39.88, 41.03)], DUDE),
    ('next', [(43.28, 43.73)], DUDE), ('staydown', [(48.71, 49.51)], DUDE), ('tooslow', [(54.65, 55.23)], DUDE),
    ('locked', [(63.47, 65.44)], DUDE), ('switch', [(67.92, 69.36)], DUDE), ('comeon', [(77.26, 78.14)], DUDE),
    ('cleansweep', [(84.35, 85.04)], DUDE), ('whosnext', [(90.47, 91.44)], DUDE),
    ('cake', [(94.28, 95.23)], ANN), ('rock', [(97.37, 98.11)], ANN), ('holyshit', [(104.48, 106.36)], ANN),
]
preview = []
for name, clips, p in lines:
    x = np.concatenate([cut(a, b) for a, b in clips])
    if name == 'locked': x = shorten_gap(x)
    y = fx(x, name, p, extra=',atempo=1.45' if name == 'holyshit' else '')     # UT99's is quicker than I said it
    if name == 'holyshit': y = echo_tail(y)
    save(y, name)
    print('%-11s %.2f s' % (name, len(y) / SR))
    preview += [y, np.zeros(int(0.6 * SR))]
# a third grunt: the second one a little higher and quicker
subprocess.run(['ffmpeg', '-v', 'quiet', '-y', '-i', 'out/grunt2.wav', '-af', 'rubberband=pitch=1.12:tempo=1.1', '-ar', str(SR), 'out/grunt3.wav'], check=True)
ww = wave.open('out/grunt3.wav'); g3 = np.frombuffer(ww.readframes(ww.getnframes()), np.int16).astype(float)
preview = [g3, np.zeros(int(0.6 * SR))] + preview
save(np.concatenate(preview), 'preview')
print('total voice: %.1f s' % (sum(len(p) for p in preview[::2]) / SR))
