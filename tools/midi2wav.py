"""Render a General MIDI file to a 44.1 kHz stereo WAV with a soundfont (TinySoundFont).  usage: midi2wav.py in.mid out.wav sf2"""
import sys, wave, numpy as np, tinysoundfont as T
mid, out, sf = sys.argv[1:4]
SR = 44100
syn = T.Synth(samplerate=SR, gain=-3); sfid = syn.sfload(sf); syn.start(buffer_size=0) if False else None
seq = T.Sequencer(syn); seq.midi_load(mid)
chunks = []; BLK = 512; tail = 0
while True:
    left = BLK / SR
    while left > 1e-9:
        adv = seq.process(left)
        if adv <= 0: break
        left -= adv
    buf = np.frombuffer(syn.generate(BLK), dtype=np.float32).copy()
    chunks.append(buf)
    if seq.is_empty():
        tail += 1
        if tail > 3 * SR // BLK: break       # 3 s of release after the last event
a = np.concatenate(chunks).reshape(-1, 2)
pk = np.abs(a).max(); a = a / max(pk, 1e-6) * 0.89
w = wave.open(out, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
w.writeframes((a * 32767).astype('<i2').tobytes()); w.close()
print(out, '%.1f s, peak %.3f' % (len(a) / SR, pk))
