#!/usr/bin/env python3
"""mksound.py - Cube's sounds as one bank for the PS1 sound chip (SPU).

Cube registers its sounds by number in data/sounds.cfg (S_JUMP = 0 ... S_JUMPPAD = 50); the bank keeps that order, so a
sound's number in the game is Cube's.  Each sound is decoded (wav / ogg via ffmpeg), made mono, trimmed of silence at
both ends, resampled if it is over the rate cap, and encoded as SPU ADPCM: 16-byte blocks of 28 samples, a shift and
one of five prediction filters per block, chosen by trying them all.  The last block carries the end flag (the voice
stops by itself).

SOUND.PAK layout (little endian):
  "SND1", u16 count, u16 0
  count x { u32 offset (bytes into the data), u32 size (bytes), u16 rate (Hz), u16 0 }
  data: the ADPCM blocks of every sound, each sound starting on a 64-byte boundary (SPU DMA unit)

The voice lines (assets/voice/*.wav, cut from a recording by tools/voicecut.py) go in a second bank of the same layout,
at 11025 Hz, in the order of VOICE below; the game numbers them on from Cube's 51 (cube.h's V_* names follow it).
Two files because each has to fit the game's 416 KB load buffer.

usage: mksound.py sound.pak voice.pak [--rate-cap 22050] [--wav check.wav]   (--wav: decode it all back for listening)"""
import argparse, os, re, struct, subprocess, sys
import numpy as np

CUBE = '/home/claude/cube_tgz/cube'
VOICE = ['grunt1', 'grunt2', 'grunt3', 'die', 'ohyeah', 'easy', 'hurt', 'next', 'staydown', 'tooslow', 'locked', 'switch', 'comeon',
         'cleansweep', 'whosnext', 'cake', 'rock', 'holyshit']
VOICE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'voice')
FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]     # SPU ADPCM prediction (x / 64)


def load(path, rate):
    raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', path, '-ac', '1', '-ar', str(rate), '-f', 's16le', '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.int16).astype(np.int32)


def native_rate(path):
    out = subprocess.run(['ffprobe', '-v', 'quiet', '-show_entries', 'stream=sample_rate', '-of', 'csv=p=0', path],
                         capture_output=True, text=True).stdout.strip().split('\n')[0]
    return int(out)


def trim(x, thr=96):
    idx = np.nonzero(np.abs(x) > thr)[0]
    if len(idx) == 0: return x[:28]
    a = max(0, idx[0] - 16); b = min(len(x), idx[-1] + 64)
    return x[a:b]


def encode(x):
    """SPU ADPCM; returns bytes (16 per 28 samples) and the decoded samples (for checking)"""
    n = (len(x) + 27) // 28 * 28
    x = np.concatenate([x, np.zeros(n - len(x), dtype=np.int32)])
    out = bytearray(); dec = []
    s1 = s2 = 0
    for b in range(n // 28):
        blk = [int(v) for v in x[b * 28:(b + 1) * 28]]
        best = None
        for f, (k1, k2) in enumerate(FILTERS):
            for sh in range(13):
                p1, p2 = s1, s2; err = 0; nib = []; ys = []
                for v in blk:
                    pred = (p1 * k1 + p2 * k2 + 32) >> 6
                    d = v - pred
                    q = (d * (1 << sh) + (1 << 11)) >> 12 if sh <= 12 else 0
                    q = max(-8, min(7, int(q)))
                    y = ((q << 12) >> sh) + pred
                    y = max(-32768, min(32767, y))
                    err += (v - y) * (v - y)
                    if best is not None and err >= best[0]: break
                    nib.append(q & 15); ys.append(y); p2, p1 = p1, y
                else:
                    if best is None or err < best[0]: best = (err, f, sh, nib, ys, p1, p2)
        err, f, sh, nib, ys, s1, s2 = best
        flags = 1 if b == n // 28 - 1 else 0                       # last block: loop end, no repeat (the voice stops)
        out += bytes([(sh & 15) | (f << 4), flags]) + bytes(nib[i] | (nib[i + 1] << 4) for i in range(0, 28, 2))
        dec += ys
    return bytes(out), np.array(dec, dtype=np.int32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out'); ap.add_argument('voice_out'); ap.add_argument('--rate-cap', type=int, default=22050); ap.add_argument('--wav')
    ap.add_argument('--voice-dir', default=VOICE_DIR)   # (assets/voice_f: Stacy's)
    a = ap.parse_args()
    names = re.findall(r'registersound "([^"]+)"', open(os.path.join(CUBE, 'data', 'sounds.cfg')).read())
    table, data, mix = [], bytearray(), []
    names = names + ['voice:' + v for v in VOICE]
    banks = []
    for i, nm in enumerate(names):
        if nm == 'voice:' + VOICE[0]:                     # start the second bank
            banks.append((table, data)); table, data = [], bytearray()
        if nm.startswith('voice:'):
            path = os.path.join(a.voice_dir, nm[6:] + '.wav')
            x = trim(load(path, 11025), 64); rate = 11025
            enc, dec = encode(x)
            while len(data) % 64: data += b'\0'
            table.append((len(data), len(enc), rate)); data += enc; mix.append((rate, dec))
            print('%2d %-20s %5d Hz %5.2f s %6d bytes' % (i, nm, rate, len(x) / rate, len(enc)))
            continue
        path = next((os.path.join(CUBE, 'packages', 'sounds', nm + e) for e in ('.wav', '.ogg') if os.path.exists(os.path.join(CUBE, 'packages', 'sounds', nm + e))), None)
        if not path: table.append((0, 0, 11025)); print('%2d %-20s missing' % (i, nm)); continue
        rate = min(native_rate(path), a.rate_cap)
        x = trim(load(path, rate))
        enc, dec = encode(x)
        n = min(len(x), len(dec))
        snr = 10 * np.log10(np.sum(x[:n].astype(float) ** 2) / max(1.0, np.sum((x[:n] - dec[:n]).astype(float) ** 2)))
        while len(data) % 64: data += b'\0'
        table.append((len(data), len(enc), rate)); data += enc
        mix.append((rate, dec))
        print('%2d %-20s %5d Hz %5.2f s %6d bytes  SNR %4.1f dB' % (i, nm, rate, len(x) / rate, len(enc), snr))
    banks.append((table, data)); total = 0
    for path, (table, data) in zip((a.out, a.voice_out), banks):
        while len(data) % 64: data += b'\0'
        with open(path, 'wb') as o:
            o.write(b'SND1' + struct.pack('<HH', len(table), 0))
            for off, size, rate in table: o.write(struct.pack('<IIHH', off, size, rate, 0))
            o.write(data)
        total += len(data)
        print('%s: %d sounds, %d bytes (file %d)' % (path, len(table), len(data), os.path.getsize(path)))
    print('sound RAM: %d of %d bytes' % (total, 512 * 1024 - 0x1010))
    if a.wav:
        import wave
        w = wave.open(a.wav, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
        for rate, dec in mix:
            y = np.interp(np.arange(0, len(dec), rate / 22050), np.arange(len(dec)), dec) if rate != 22050 else dec
            w.writeframes(np.clip(y, -32768, 32767).astype(np.int16).tobytes() + b'\0\0' * 4000)
        w.close()


if __name__ == '__main__': main()
