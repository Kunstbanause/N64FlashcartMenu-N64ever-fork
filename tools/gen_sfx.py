#!/usr/bin/env python3
"""
gen_sfx.py - generate N64ever's custom UI sound effects from pure synthesis.

These five SFX are ORIGINAL works, synthesized here from first principles (sine
tones + amplitude envelopes, Python standard library only - no samples, no
third-party audio). They are free to use. Re-running this script reproduces the
exact same WAV files, so the provenance is fully documented and reproducible.

Output: 44100 Hz, mono, 16-bit PCM WAV (the format the build's audioconv64 step
expects). Writes grid_move / grid_enter / grid_back / launch / settings into the
assets/sounds directory (override with: gen_sfx.py <output_dir>).

The other bundled sounds (back, bgm, cursorsound, enter, error) are NOT touched -
they come from upstream N64FlashcartMenu and keep their own attributions.
"""
import math
import os
import struct
import sys
import wave

SR = 44100


def adsr(total, attack=0.004, decay_tau=0.05, release=0.004):
    """Linear attack, exponential decay, short linear release (anti-click)."""
    a = max(1, int(attack * SR))
    r = min(total, max(1, int(release * SR)))
    e = []
    for n in range(total):
        amp = n / a if n < a else math.exp(-(n - a) / (decay_tau * SR))
        e.append(amp)
    for i in range(r):
        e[total - 1 - i] *= i / r
    return e


def sweep(total, f0, f1, env, h2=0.0):
    """Exponential (musical) frequency sweep f0 -> f1, optional 2nd harmonic."""
    buf = [0.0] * total
    phase = 0.0
    for n in range(total):
        f = f0 * (f1 / f0) ** (n / total)
        phase += 2.0 * math.pi * f / SR
        s = math.sin(phase) + h2 * math.sin(2.0 * phase)
        buf[n] = s * env[n]
    return buf


def add_note(buf, start_t, dur, freq, vol, attack=0.004, decay_tau=0.06, h2=0.25):
    """Mix a decaying tone into buf starting at start_t seconds."""
    start = int(start_t * SR)
    total = int(dur * SR)
    e = adsr(total, attack=attack, decay_tau=decay_tau)
    phase = 0.0
    for n in range(total):
        idx = start + n
        if idx >= len(buf):
            break
        phase += 2.0 * math.pi * freq / SR
        s = math.sin(phase) + h2 * math.sin(2.0 * phase)
        buf[idx] += vol * s * e[n]


def normalize(buf, peak):
    m = max(1e-9, max(abs(x) for x in buf))
    return [x / m * peak for x in buf]


def write_wav(path, buf):
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for s in buf:
            v = max(-1.0, min(1.0, s))
            frames += struct.pack("<h", int(round(v * 32767)))
        w.writeframes(bytes(frames))
    print("  wrote", os.path.basename(path), "(%.3fs)" % (len(buf) / SR))


def grid_move():
    """Soft, subtle cursor tick - a quick downward sine blip."""
    total = int(0.055 * SR)
    e = adsr(total, attack=0.002, decay_tau=0.012)
    buf = sweep(total, 1500, 1180, e, h2=0.12)
    return normalize(buf, 0.50)


def grid_enter():
    """Confirming 'blip up' - rising sweep."""
    total = int(0.075 * SR)
    e = adsr(total, attack=0.003, decay_tau=0.03)
    buf = sweep(total, 720, 1320, e, h2=0.22)
    return normalize(buf, 0.72)


def grid_back():
    """Cancel 'blip down' - falling sweep (the inverse gesture of enter)."""
    total = int(0.075 * SR)
    e = adsr(total, attack=0.003, decay_tau=0.03)
    buf = sweep(total, 1300, 650, e, h2=0.22)
    return normalize(buf, 0.70)


def launch():
    """Rewarding power-up flourish - a quick ascending C-major arpeggio."""
    total = int(0.34 * SR)
    buf = [0.0] * total
    add_note(buf, 0.00, 0.13, 523.25, 0.45, decay_tau=0.06)   # C5
    add_note(buf, 0.07, 0.13, 659.25, 0.45, decay_tau=0.06)   # E5
    add_note(buf, 0.14, 0.14, 783.99, 0.45, decay_tau=0.07)   # G5
    add_note(buf, 0.21, 0.13, 1046.50, 0.50, decay_tau=0.10)  # C6
    return normalize(buf, 0.90)


def settings():
    """Neutral two-step 'ding-ding' toggle blip."""
    total = int(0.075 * SR)
    buf = [0.0] * total
    add_note(buf, 0.000, 0.035, 987.77, 0.5, decay_tau=0.02, h2=0.15)   # B5
    add_note(buf, 0.038, 0.037, 1318.51, 0.5, decay_tau=0.02, h2=0.15)  # E6
    return normalize(buf, 0.60)


SOUNDS = {
    "grid_move": grid_move,
    "grid_enter": grid_enter,
    "grid_back": grid_back,
    "launch": launch,
    "settings": settings,
}


def main():
    if len(sys.argv) > 1:
        out_dir = sys.argv[1]
    else:
        out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "..", "assets", "sounds")
    out_dir = os.path.abspath(out_dir)
    print("Generating N64ever UI SFX into", out_dir)
    for name, fn in SOUNDS.items():
        write_wav(os.path.join(out_dir, name + ".wav"), fn())


if __name__ == "__main__":
    main()
