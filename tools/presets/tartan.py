# SPDX-License-Identifier: MPL-2.0
"""tartan's presets (2026-11-23): sixteen of Chordism's (schwung-chordism, the user's, MIT), converted by chd() from its
values (0..100 where Chordism's run 0..10000): the chord and its voicing, the waves, the morphs and the shimmer onto
tartan's three pages; its filter, envelope, vibrato, delay and reverb onto LOOM-8's own pages (its times, 1 ms .. 16 s,
onto LOOM-8's 1 ms .. 10 s; its cutoff, 20 Hz .. 20 kHz, onto LOOM-8's 50 Hz .. 20 kHz; its AD mode a sustain of 0)."""
import math

from lib import P

CHORDS = ["octaves", "fifth", "minor", "min7", "min9", "min11", "major", "maj7", "maj9", "sus4", "6/9", "min6", "10th",
          "dom7", "7b9", "half-dim"]
WAVES = {"saw": 0, "sine": 1, "pulse": 2, "square": 3, "glass": 4, "harmonic": 5, "saw sine": 6, "pls sine": 7,
         "sine pls": 8, "pls saw": 9, "saw duo": 10, "mixed": 11}


def u(x, k=1.27):
    return max(0, min(127, round(x * k)))


def cut(c):                                  # Chordism's 0..100 (20 Hz .. 20 kHz) to LOOM-8's knob (50 Hz .. 20 kHz)
    hz = 20.0 * 2 ** (c / 100 * 9.966)
    return max(0, min(127, round(math.log2(max(hz, 50.0) / 50.0) / 8.6439 * 127)))


def t(x):                                    # a time, Chordism's 0..100 (1 ms .. 16 s), as LOOM-8's knob (1 ms .. 10 s)
    return max(0, min(127, round(x / 100 * 133.5)))


def chd(name, role, chord, sp, rot, det, waves, sh, wid, fm, mo, mi, pm, pi, lfo, ct, res, A, R, held, vib, rv, dl,
        fe, hp=False, octave=None, genres=()):
    P(name, role, "tartan", knobs=dict(chord=CHORDS.index(chord), spread=round(sp / 50 * 64), rotate=int(rot * 3.999 / 100),
                                       detune=u(det)),
      x=(WAVES[waves], u(sh), u(wid), u(fm)), morph=(u(mo), u(mi), u(pm) if pi else 0, u(lfo, 2.54)),
      amp=(t(A), t(R) if not held else 64, 127 if held else 0, t(R)),
      fenv=(0, 70, 0, 50) if fe else (0, 64, 0, 40),
      filter=("hp12" if hp else "lp12", cut(ct), u(res, 1.0), 64 + round(fe * 0.6), 64),
      sends=(u(dl, 0.9), u(rv, 0.9)), lfo=("wobble", 92, 64 + round(vib * 0.35), 64, 20, 0) if vib else None,
      volume=100, octave=octave, genres=genres)


# its sound in a new project: Chordism's lush pad, a major ninth of saws, moving
chd("tartan", "pad", "maj9", 50, 0, 30, "saw", 40, 100, 0, 40, 50, 50, 60, 40, 50, 40, 71, 80, 1, 30, 55, 10, 30,
    genres=("synthpop", "lofi"))
chd("fm bell", "bell", "6/9", 50, 0, 5, "sine", 0, 80, 40, 20, 30, 0, 0, 0, 70, 0, 0, 79, 0, 0, 35, 0, 0)
chd("plucky lead", "lead", "dom7", 50, 0, 0, "saw", 20, 60, 0, 0, 0, 0, 0, 0, 45, 0, 0, 73, 0, 0, 20, 15, 85)
chd("drone", "pad", "octaves", 50, 0, 10, "glass", 45, 50, 0, 30, 25, 0, 0, 30, 50, 50, 78, 83, 1, 20, 70, 30, 0,
    genres=("lofi",))
chd("lofi stab", "stab", "min7", 50, 0, 0, "saw", 0, 70, 0, 0, 0, 0, 0, 0, 40, 0, 0, 71, 0, 0, 20, 0, 40,
    genres=("lofi", "house"))
chd("glacial", "pad", "maj9", 50, 0, 20, "saw", 30, 60, 0, 30, 40, 20, 30, 30, 45, 20, 74, 84, 1, 10, 75, 20, 10)
chd("acid chord", "lead", "fifth", 40, 0, 0, "saw duo", 10, 40, 0, 0, 0, 0, 0, 0, 25, 75, 0, 69, 0, 0, 20, 20, 55)
chd("chord keys", "keys", "maj7", 50, 0, 5, "sine", 42, 50, 0, 20, 30, 0, 0, 0, 50, 12, 0, 74, 0, 0, 40, 10, 55)
chd("poly brass", "brass", "sus4", 50, 0, 10, "saw", 10, 70, 0, 0, 0, 0, 0, 0, 40, 15, 54, 74, 1, 0, 35, 0, 50)
chd("underwater", "pad", "maj9", 60, 0, 15, "glass", 50, 70, 0, 50, 50, 40, 70, 40, 50, 40, 78, 82, 1, 0, 55, 35, 20,
    genres=("lofi",))
chd("pulse stab", "stab", "dom7", 50, 0, 0, "pulse", 30, 50, 0, 0, 0, 0, 0, 0, 30, 15, 0, 62, 0, 0, 15, 0, 65,
    genres=("house",))
chd("wave glow", "pad", "10th", 50, 0, 10, "harmonic", 50, 70, 0, 20, 30, 0, 0, 20, 45, 20, 54, 74, 1, 5, 35, 15, 40)
chd("music box", "bell", "maj7", 50, 0, 2, "sine", 0, 100, 32, 0, 0, 0, 0, 0, 85, 5, 0, 64, 0, 0, 40, 0, 0)
chd("glass chime", "bell", "maj9", 50, 0, 0, "sine pls", 0, 100, 25, 30, 40, 0, 0, 0, 70, 0, 0, 61, 0, 0, 35, 0, 0)
chd("filter run", "fx", "minor", 50, 0, 0, "saw", 0, 100, 0, 0, 0, 0, 0, 0, 10, 50, 30, 61, 1, 0, 25, 0, 70)
# (Chordism's bloom opens its filter by aftertouch, from 12: here open to 42, its filter envelope opening it more)
chd("bloom", "pad", "6/9", 50, 0, 15, "saw", 30, 90, 0, 0, 0, 0, 0, 0, 42, 30, 61, 75, 1, 0, 50, 0, 40)
