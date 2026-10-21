#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The wavetable engine's nine tables (shapes, nasal, grit, vocal, spiral, fold, octaves, glass,
chirp), 16 frames x 256 int8 samples each (36 KB in flash: 8 bits, the quantising some 48 dB under
each frame's peak, below what the linear read itself leaves).

    tools/gen_wavetables.py build/gen/wavetables.h

Every frame is built from a harmonic recipe or drawn in the time domain and then band-limited to
NHARM harmonics through a plain 256-point DFT (no numpy), so the device plays them with a linear
read and no gross aliasing below about C6. Frames are peak-normalised so the position knob does
not change the level. The header defines the table (synth.c includes it once; synth.c WTNAME_N
names them in this order). Each table's character follows its name:
  shapes   sine -> triangle -> saw -> square, the classic shapes in a row
  nasal    a saw with a resonant peak climbing the harmonics: nasal, then buzzing
  grit     a sine quantised to fewer steps and sample-held: digital grit
  vocal    a saw's harmonics through two formant peaks, the vowels a -> e -> i -> o -> u
  spiral   partials at the Fibonacci numbers 1 2 3 5 8 13 21 34, one more each frame
  fold     a sine through a wavefolder, its gain rising from 1x to 6x
  octaves  octave stacks 1 2 4 8 16 32 in a geometric series, from a near sine to equal octaves
  glass    the fundamental plus partials at the primes, one more each frame
  chirp    a chirp inside the cycle, high to low, steeper each frame"""
import math
import sys

N = 256
FRAMES = 16
NHARM = 40
TWO_PI = 2.0 * math.pi

TABLES = ["shapes", "nasal", "grit", "vocal", "spiral", "fold", "octaves", "glass", "chirp"]


def from_harmonics(amp, phase=None):
    """amp[k] = amplitude of harmonic k+1"""
    out = []
    for i in range(N):
        t = i / N
        s = 0.0
        for k, a in enumerate(amp[:NHARM]):
            if a:
                ph = phase[k] if phase else 0.0
                s += a * math.sin(TWO_PI * (k + 1) * t + ph)
        out.append(s)
    return out


def bandlimit(x):
    """keep harmonics 1..NHARM of a time-domain cycle (DFT, 256 points)"""
    re = []
    im = []
    for k in range(1, NHARM + 1):
        sr = si = 0.0
        for i in range(N):
            a = TWO_PI * k * i / N
            sr += x[i] * math.cos(a)
            si -= x[i] * math.sin(a)
        re.append(sr * 2.0 / N)
        im.append(si * 2.0 / N)
    out = []
    for i in range(N):
        s = 0.0
        for k in range(NHARM):
            a = TWO_PI * (k + 1) * i / N
            s += re[k] * math.cos(a) - im[k] * math.sin(a)
        out.append(s)
    return out


def normalise(x):
    p = max(abs(v) for v in x) or 1.0
    return [v / p for v in x]


def lerp(a, b, t):
    return a + (b - a) * t


def tri_amp():
    amp = [0.0] * NHARM
    for k in range(NHARM):
        n = k + 1
        if n % 2 == 1:
            amp[k] = (1.0 / (n * n)) * (1.0 if (n // 2) % 2 == 0 else -1.0)
    return amp


def saw_amp():
    return [1.0 / (k + 1) for k in range(NHARM)]


def square_amp():
    return [1.0 / (k + 1) if (k + 1) % 2 == 1 else 0.0 for k in range(NHARM)]


# -------------------------------------------------------------------- the tables ---
def t_shapes(f):
    """sine -> triangle -> saw -> square over the 16 frames"""
    sine = [1.0] + [0.0] * (NHARM - 1)
    shapes = [sine, tri_amp(), saw_amp(), square_amp()]
    u = f / (FRAMES - 1) * 3.0
    i0 = min(int(u), 2)
    w = u - i0
    return from_harmonics([lerp(shapes[i0][k], shapes[i0 + 1][k], w) for k in range(NHARM)])


def t_nasal(f):
    """a saw whose spectrum carries a resonant peak, climbing from the 2nd to the 28th harmonic"""
    u = f / (FRAMES - 1)
    c = lerp(2.0, 28.0, u)
    w = 1.5 + c * 0.18
    amp = []
    for k in range(NHARM):
        n = k + 1
        amp.append((1.0 / n) * (1.0 + 7.0 * math.exp(-((n - c) / w) ** 2)))
    return from_harmonics(amp)


def t_grit(f):
    """a sine quantised to fewer and fewer steps, then sample-and-held: aliasing by design, tamed by the DFT"""
    levels = max(2, int(round(lerp(64, 2, f / (FRAMES - 1)))))
    hold = 1 + f // 2
    x = []
    for i in range(N):
        j = (i // hold) * hold
        s = math.sin(TWO_PI * j / N)
        x.append(round(s * levels * 0.5) / (levels * 0.5))
    return bandlimit(x)


# the vowels' first two formants (Hz, an adult voice's averages): a e i o u
VOWELS = [(730.0, 1090.0), (530.0, 1840.0), (270.0, 2290.0), (570.0, 840.0), (300.0, 870.0)]
VOCAL_F0 = 130.0                                     # the formants sit where they belong at C3


def formant(hz, centre, bw):
    """a resonance's weight: 1 at its centre, half at +-bw, falling as 1 / distance^2 past it (a
    two-pole's power), so the peak stands out of the saw's slope"""
    return 1.0 / (1.0 + ((hz - centre) / bw) ** 2)


def t_vocal(f):
    """a saw's harmonics weighted by two formant peaks that glide a -> e -> i -> o -> u over the frames
    (log-frequency steps between the vowels); harmonic n sits at n x VOCAL_F0"""
    u = f / (FRAMES - 1) * (len(VOWELS) - 1)
    i0 = min(int(u), len(VOWELS) - 2)
    w = u - i0
    f1 = math.exp(lerp(math.log(VOWELS[i0][0]), math.log(VOWELS[i0 + 1][0]), w))
    f2 = math.exp(lerp(math.log(VOWELS[i0][1]), math.log(VOWELS[i0 + 1][1]), w))
    amp = []
    for k in range(NHARM):
        n = k + 1
        hz = n * VOCAL_F0
        amp.append((1.0 / n) * (0.03 + formant(hz, f1, 110.0) + formant(hz, f2, 160.0)))
    return from_harmonics(amp)


FIB = [1, 2, 3, 5, 8, 13, 21, 34]


def t_spiral(f):
    """partials at the Fibonacci numbers; frame f has the first 1 + f * 7 / 15 of them (the last fading in)"""
    cnt = 1.0 + f * 7.0 / (FRAMES - 1)
    amp = [0.0] * NHARM
    for j, n in enumerate(FIB):
        g = min(1.0, max(0.0, cnt - j))
        if n <= NHARM and g > 0.0:
            amp[n - 1] = g / math.sqrt(j + 1)
    return from_harmonics(amp, [0.61 * j * j for j in range(NHARM)])


def fold(x):
    """a triangle wavefolder: x inside +-1 passes, past it reflects back (and again), never leaving +-1"""
    return 1.0 - 4.0 * abs(((x + 1.0) / 4.0) % 1.0 - 0.5)


def t_fold(f):
    """a sine through the wavefolder, the gain into it rising from 1x (the sine itself) to 6x"""
    gain = lerp(1.0, 6.0, f / (FRAMES - 1))
    return bandlimit([fold(gain * math.sin(TWO_PI * i / N)) for i in range(N)])


def t_octaves(f):
    """octaves 1 2 4 8 16 32 with amplitudes r^k: r from 0.08 (a sine with a whisper of its octave) to 1"""
    r = lerp(0.08, 1.0, f / (FRAMES - 1))
    amp = [0.0] * NHARM
    for k in range(6):
        n = 2 ** k
        if n <= NHARM:
            amp[n - 1] = r ** k
    return from_harmonics(amp, [0.0, 0.5, 1.1, 1.9, 2.6, 0.3] + [0.0] * (NHARM - 6))


PRIMES = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]


def t_glass(f):
    """the fundamental plus partials at the primes, 0 .. 12 of them over the frames"""
    cnt = f * 12.0 / (FRAMES - 1)
    amp = [0.0] * NHARM
    amp[0] = 1.0
    for j, p in enumerate(PRIMES):
        g = min(1.0, max(0.0, cnt - j))
        if p <= NHARM and g > 0.0:
            amp[p - 1] = g * 0.8 / math.sqrt(p)
    return from_harmonics(amp, [0.37 * k * k for k in range(NHARM)])


def t_chirp(f):
    """a chirp inside each cycle: the phase runs fast at the start and slow at the end, steeper each frame"""
    k = lerp(0.0, 11.0, f / (FRAMES - 1))
    x = []
    for i in range(N):
        t = i / N
        ph = t + k * t * (1.0 - t)                  # rate 1 + k (1 - 2 t): high, then low; whole at t = 1
        x.append(math.sin(TWO_PI * ph))
    return bandlimit(x)


GEN = [t_shapes, t_nasal, t_grit, t_vocal, t_spiral, t_fold, t_octaves, t_glass, t_chirp]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/gen/wavetables.h"
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* generated by tools/gen_wavetables.py: %d tables x %d frames x %d samples; do not edit */" % (len(GEN), FRAMES, N),
             "#pragma once", "#include <stdint.h>",
             "#define WT_NTABLE %d" % len(GEN), "#define WT_NFRAME %d" % FRAMES, "#define WT_N %d" % N,
             "const int8_t WT[%d][%d][%d] = {" % (len(GEN), FRAMES, N)]
    for ti, gen in enumerate(GEN):
        lines.append("{ /* %s */" % TABLES[ti])
        for f in range(FRAMES):
            x = normalise(gen(f))
            vals = [int(max(-127, min(127, round(v * 125)))) for v in x]   # (the level as at 16 bits: 125/128 = 32000/32768)
            rows = []
            for i in range(0, N, 16):
                rows.append(",".join("%d" % v for v in vals[i:i + 16]))
            lines.append("{" + ",\n".join(rows) + "},")
        lines.append("},")
    lines.append("};")
    with open(out, "w") as fp:
        fp.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
