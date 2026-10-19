#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The demo songs' user sample banks (tools/demos/*.xy load them with "samples build/demos/<song>.bin").

  make_banks.py BREAKS_DIR OUT_DIR [SONG ...]

For each song: its WAVs into OUT_DIR/<song>/ (breaks cut to one bar from BREAKS_DIR, the rest synthesized here:
sung vocal phrases on a sixteenth grid, an 808, a horn stab, a "hey"), then the bank image OUT_DIR/<song>.bin by
tools/loom_samples.py build, exactly as the FM-1 would be loaded (IMA ADPCM in the 216 KB bank): the loops the slice
engine cuts at 32000 Hz (LOOP: brighter cuts; a bar at 32 kHz is about 35 KB), the one-shots at the rate the
tool chooses (22050 Hz: they fit). Every bank fits the device's bank (about 70 KB the largest); the tool refuses one
that does not.

The breaks are classic records (schwung-breakbeat's samples/): for private listening only. Nothing this script makes
may be committed, published or put on a site: OUT_DIR is under build/ (git-ignored).
Needs numpy and scipy.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import scipy.io.wavfile as wf
import scipy.signal as ss

SR = 44100
HERE = Path(__file__).resolve().parent
TOOL = HERE.parent / "loom_samples.py"
RNG = np.random.default_rng(7)
LOOP = "@1%32k"                                       # a one-bar loop for the slice engine, at 32000 Hz


def write(path, x):
    x = np.asarray(x, dtype=np.float64)
    x = x / (np.abs(x).max() + 1e-9) * 0.95
    wf.write(str(path), SR, (x * 32767).astype(np.int16))


def mono(path):
    sr, x = wf.read(str(path))
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != SR:
        x = ss.resample_poly(x, SR, sr)
    return x / (np.abs(x).max() + 1e-9)


def bar_of(path, bars_in_file, bar=0, fade_ms=3.0):
    """one bar of a loop trimmed to whole bars: bar `bar` of `bars_in_file`, a short fade at the end"""
    x = mono(path)
    n = len(x) // bars_in_file
    y = x[bar * n:(bar + 1) * n].copy()
    f = int(SR * fade_ms / 1000)
    y[-f:] *= np.linspace(1, 0, f)
    return y


# ---- a small formant singer -------------------------------------------------------------------------------
# soprano and alto formant tables (frequency Hz, bandwidth Hz, level dB), five formants a vowel
VOW = {
    "soprano": {
        "a": ([800, 1150, 2900, 3900, 4950], [80, 90, 120, 130, 140], [0, -6, -32, -20, -50]),
        "e": ([350, 2000, 2800, 3600, 4950], [60, 100, 120, 150, 200], [0, -20, -15, -40, -56]),
        "i": ([270, 2140, 2950, 3900, 4950], [60, 90, 100, 120, 120], [0, -12, -26, -26, -44]),
        "o": ([450, 800, 2830, 3800, 4950], [70, 80, 100, 130, 135], [0, -11, -22, -22, -50]),
        "u": ([325, 700, 2700, 3800, 4950], [50, 60, 170, 180, 200], [0, -16, -35, -40, -60]),
        "m": ([280, 1100, 2400, 3500, 4950], [60, 200, 300, 300, 300], [0, -22, -34, -44, -60]),
    },
    "alto": {
        "a": ([800, 1150, 2800, 3500, 4950], [80, 90, 120, 130, 140], [0, -4, -20, -36, -60]),
        "e": ([400, 1600, 2700, 3300, 4950], [60, 80, 120, 150, 200], [0, -24, -30, -35, -60]),
        "i": ([350, 1700, 2700, 3700, 4950], [50, 100, 120, 150, 200], [0, -20, -30, -36, -60]),
        "o": ([450, 800, 2830, 3500, 4950], [70, 80, 100, 130, 135], [0, -9, -16, -28, -55]),
        "u": ([325, 700, 2530, 3500, 4950], [50, 60, 170, 180, 200], [0, -12, -30, -40, -64]),
        "m": ([260, 1000, 2300, 3300, 4950], [60, 200, 300, 300, 300], [0, -22, -34, -44, -60]),
    },
}
# a few vowels between: "ae" (baby), "ei" (say, hey: e gliding to i), "ai" (I), "ou" (go, oh)
GLIDES = {"ei": ("e", "i"), "ai": ("a", "i"), "ou": ("o", "u"), "ae": ("a", "e"), "ia": ("i", "a")}
NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(n):
    if isinstance(n, (int, float)):
        return float(n)
    k, i = NOTE[n[0].upper()], 1
    if n[1:2] == "#":
        k, i = k + 1, 2
    elif n[1:2] == "b":
        k, i = k - 1, 2
    return float(k + 12 * (int(n[i:]) + 1))


def sing(syl, bpm, steps=16, voice="alto", fshift=1.0, vib=True, glide_ms=35.0, breath=0.04, hard=False, air=0.0):
    """a phrase on the sixteenth grid: syl = [(start16, len16, consonant, vowel, note), ...], `steps` sixteenths
    long at bpm. consonant: '' b d g k t p s sh h m n l w y; vowel a e i o u m (a hum) or a glide (ei ai ou ae ia).
    hard: no glide, no vibrato (the tuned-hard sound). air: the breath above the formants (5-10 kHz) as a
    microphone close to a singer hears it, its level against the voice (0 = none: the formants stop at 5 kHz)."""
    six = 60.0 / bpm / 4.0
    n = int(round(steps * six * SR))
    tab = VOW[voice]
    f0 = np.zeros(n)
    amp = np.zeros(n)
    noi = np.zeros(n)                                   # consonant noise, before its filter
    noi_hp = np.zeros(n)                                # sibilance, high-passed
    fq = np.zeros((n, 5))
    bw = np.zeros((n, 5))
    lv = np.zeros((n, 5))
    last = None
    for (st, ln, con, vow, note) in syl:
        a, b = int(st * six * SR), min(n, int((st + ln) * six * SR))
        L = b - a
        c_len = {"": 0, "b": 0.02, "d": 0.02, "g": 0.025, "k": 0.03, "t": 0.03, "p": 0.025, "s": 0.09, "sh": 0.09,
                 "h": 0.05, "m": 0.06, "n": 0.05, "l": 0.04, "w": 0.05, "y": 0.04}[con]
        cl = min(int(c_len * SR), L // 3)
        v0, v1 = GLIDES.get(vow, (vow, vow))
        F0, B0, L0 = (np.array(t, float) for t in tab[v0])
        F1, B1, L1 = (np.array(t, float) for t in tab[v1])
        t = np.linspace(0, 1, L)[:, None]
        g = np.clip((t - 0.35) / 0.5, 0, 1)             # a glide's move, over the middle of the vowel
        fq[a:b] = F0 + (F1 - F0) * g
        bw[a:b] = B0 + (B1 - B0) * g
        lv[a:b] = L0 + (L1 - L0) * g
        if con in ("m", "n", "l", "w", "y"):            # voiced consonants: their own colour first
            Fc = {"m": tab["m"][0], "n": [280, 1700, 2600, 3500, 4950], "l": [360, 1300, 2700, 3500, 4950],
                  "w": tab["u"][0], "y": tab["i"][0]}[con]
            mix = np.linspace(1, 0, cl)[:, None]
            fq[a:a + cl] = np.array(Fc, float) * mix + fq[a:a + cl] * (1 - mix)
        m = midi(note)
        hz = 440.0 * 2 ** ((m - 69) / 12)
        f0[a:b] = hz
        env = np.ones(L)
        att = int(0.012 * SR) if con not in ("m", "n", "l", "w", "y") else int(0.03 * SR)
        rel = min(int(0.05 * SR), L // 3)
        env[:att] = np.linspace(0, 1, att)
        env[-rel:] *= np.linspace(1, 0, rel) ** 1.5
        if con in ("b", "d", "g", "k", "t", "p", "s", "sh", "h"):
            env[:cl] *= 0.0 if con in ("k", "t", "p", "s", "sh", "h") else 0.4
            env[cl:cl + att] *= np.linspace(0, 1, att)
            burst = np.zeros(L)
            if con in ("s", "sh"):
                burst[:cl] = np.hanning(cl) * (0.5 if con == "s" else 0.4)
                (noi_hp if con == "s" else noi)[a:b] += burst * RNG.standard_normal(L)
            elif con == "h":
                burst[:cl] = np.hanning(cl) * 0.35
                noi[a:b] += burst * RNG.standard_normal(L)
            else:
                k = min(cl, int(0.012 * SR))
                burst[:k] = np.exp(-np.linspace(0, 6, k)) * (0.6 if con in ("k", "t", "p") else 0.25)
                noi_hp[a:b] += burst * RNG.standard_normal(L)
        if con in ("m", "n"):
            env[:cl] *= 0.5
        amp[a:b] = np.maximum(amp[a:b], env)
        last = hz
    # the pitch: a glide between notes, a vibrato coming in late on long notes, a little drift
    if not hard:
        k = int(glide_ms / 1000 * SR)
        f0s = f0.copy()
        voiced = f0 > 0
        f0s[~voiced] = np.interp(np.flatnonzero(~voiced), np.flatnonzero(voiced), f0[voiced]) if voiced.any() else 0
        f0 = ss.lfilter([1.0 / k], [1, -(1 - 1.0 / k)], f0s, zi=[f0s[0] * (1 - 1.0 / k)])[0]
    else:
        voiced = f0 > 0
        f0[~voiced] = np.interp(np.flatnonzero(~voiced), np.flatnonzero(voiced), f0[voiced])
    tt = np.arange(n) / SR
    if vib and not hard:
        # vibrato grows within each voiced stretch: samples since the voice last stopped
        v = amp > 0.05
        idx = np.arange(n)
        start = np.maximum.accumulate(np.where(~v, idx, 0))
        on = np.where(v, idx - start, 0)
        depth = np.clip((on / SR - 0.18) / 0.3, 0, 1) * 0.35     # semitones
        f0 = f0 * 2 ** (depth * np.sin(2 * np.pi * 5.4 * tt) / 12)
    f0 = f0 * 2 ** (0.04 * np.cumsum(RNG.standard_normal(n)) / np.sqrt(SR) / 12)
    # the source: a band-limited pulse (harmonics falling 9 dB an octave), its phase from f0
    ph = 2 * np.pi * np.cumsum(f0) / SR
    src = np.zeros(n)
    fmin = max(80.0, f0.min())
    for h in range(1, int(9000 / fmin) + 1):
        a = (h ** -1.5) * (h * f0 < 9000)
        src += a * np.sin(h * ph)
    src *= amp
    src += breath * RNG.standard_normal(n) * amp
    exc = src + noi
    # the formants, time-varying: two-pole resonators, the coefficients per 32-sample block
    out = np.zeros(n)
    blk = 32
    for f in range(5):
        zi = np.zeros(2)
        y = np.zeros(n)
        for s0 in range(0, n, blk):
            s1 = min(n, s0 + blk)
            F = fq[s0:s1, f].mean() * fshift
            B = max(bw[s0:s1, f].mean(), 40.0)
            G = 10 ** (lv[s0:s1, f].mean() / 20.0)
            if F <= 0 or F > SR * 0.45:
                zi[:] = 0
                continue
            r = np.exp(-np.pi * B / SR)
            c = 2 * r * np.cos(2 * np.pi * F / SR)
            y[s0:s1], zi = ss.lfilter([G * (1 - r)], [1, -c, r * r], exc[s0:s1], zi=zi)
        out += y
    hp = ss.butter(4, 5000, "high", fs=SR, output="sos")
    out += ss.sosfilt(hp, noi_hp) * 0.6
    if air > 0:                                         # the air: breath in a band above the formants, with the voice
        band = ss.sosfilt(ss.butter(2, [5000, 10000], "band", fs=SR, output="sos"), RNG.standard_normal(n) * amp)
        out += band * air * np.sqrt((out ** 2).mean()) / (np.sqrt((band ** 2).mean()) + 1e-12)
    out = ss.sosfilt(ss.butter(2, 90, "high", fs=SR, output="sos"), out)
    return np.tanh(out / (np.abs(out).max() + 1e-9) * 1.4)


def tr808(f0=65.41, dur=1.1, drive=2.2):
    t = np.arange(int(dur * SR)) / SR
    f = f0 * (1 + 1.6 * np.exp(-t / 0.028))
    ph = 2 * np.pi * np.cumsum(f) / SR
    env = np.exp(-t / 0.42) * np.clip(t / 0.002, 0, 1)
    x = np.sin(ph) * env
    x[:int(0.004 * SR)] += RNG.standard_normal(int(0.004 * SR)) * 0.3 * np.linspace(1, 0, int(0.004 * SR))
    x = np.tanh(x * drive) / np.tanh(drive)
    x[-int(0.02 * SR):] *= np.linspace(1, 0, int(0.02 * SR))
    return x


def horn(notes=("Bb3", "D4", "F4", "Ab4"), dur=0.62):
    """a brass stab: detuned saws per note, a fast filter swell and fall, a touch of drive"""
    t = np.arange(int(dur * SR)) / SR
    x = np.zeros_like(t)
    for nm in notes:
        hz = 440 * 2 ** ((midi(nm) - 69) / 12)
        for det in (-0.08, 0.0, 0.07):
            ph = (t * hz * 2 ** (det / 12) + RNG.random()) % 1.0
            x += 2 * ph - 1
    env = np.clip(t / 0.015, 0, 1) * np.exp(-np.maximum(t - 0.08, 0) / 0.22)
    y = np.zeros_like(x)
    zi = np.zeros(2)
    for s0 in range(0, len(t), 64):
        s1 = min(len(t), s0 + 64)
        fc = 600 + 3800 * np.exp(-t[s0] / 0.12) * np.clip(t[s0] / 0.02, 0, 1)
        b, a = ss.butter(2, fc, fs=SR)
        y[s0:s1], zi = ss.lfilter(b, a, x[s0:s1], zi=zi)
    y = y * env
    return np.tanh(y / np.abs(y).max() * 1.8)


# ---- the banks -------------------------------------------------------------------------------------------------
def bank_jungle(B, out):
    """amen (bar 1 of amen01, 137.9 BPM) and think (114.4 BPM): at 170 both run fast and high, as they did"""
    write(out / "amen.wav", bar_of(B / "amen01.wav", 2))
    write(out / "think.wav", bar_of(B / "think.wav", 1))
    return ["amen=" + str(out / "amen.wav") + LOOP, "think=" + str(out / "think.wav") + LOOP]


def bank_garage(B, out):
    """a sung bar at 132 (cut in 16 by the slice engine: each sixteenth a syllable or its tail), and the sesame
    break (119 BPM) for a top loop"""
    v = sing([(0, 2, "", "ou", "Eb5"), (2, 1, "b", "ei", "C5"), (3, 2, "b", "i", "Bb4"), (6, 2, "", "ai", "G4"),
              (8, 1, "n", "i", "Bb4"), (9, 1, "d", "u", "C5"), (10, 3, "", "ou", "Eb5"), (13, 1, "y", "e", "F5"),
              (14, 2, "", "a", "Eb5")], 132, voice="alto", fshift=1.05, air=0.12)
    write(out / "vox.wav", v)
    write(out / "sesame.wav", bar_of(B / "sesame.wav", 1))
    return ["babyvox=" + str(out / "vox.wav") + LOOP, "sesame=" + str(out / "sesame.wav") + LOOP]


def bank_hyperpop(B, out):
    """a bar sung at 160, tuned hard and high (formants up: the pitched-up voice), an 808 on C2"""
    v = sing([(0, 1, "", "a", "A5"), (1, 1, "", "a", "C6"), (2, 2, "", "ai", "E6"), (4, 1, "w", "a", "D6"),
              (5, 1, "n", "a", "C6"), (6, 2, "", "ei", "A5"), (8, 1, "", "ou", "E6"), (9, 1, "", "ou", "D6"),
              (10, 1, "m", "i", "C6"), (11, 1, "", "i", "A5"), (12, 2, "y", "e", "G5"), (14, 2, "", "a", "A5")],
             160, voice="soprano", fshift=1.22, hard=True, breath=0.02, air=0.14)
    write(out / "vox.wav", v)
    write(out / "808.wav", tr808(65.41, 1.0, 3.0))
    return ["hypervox=" + str(out / "vox.wav") + LOOP, "808=" + str(out / "808.wav") + ":36"]


def bank_house(B, out):
    """the sesame break (119 BPM) to filter into a top loop, and a "hey" on A3"""
    write(out / "sesame.wav", bar_of(B / "sesame.wav", 1))
    write(out / "hey.wav", sing([(0, 3, "h", "ei", "A3")], 122, steps=4, voice="alto", glide_ms=20, air=0.06))
    return ["sesame=" + str(out / "sesame.wav") + LOOP, "hey=" + str(out / "hey.wav") + ":57"]


def bank_pop(B, out):
    """the hook sung at 110 (sliced), and an "oh" on C5 for a pitched vocal-chop instrument"""
    v = sing([(0, 2, "", "ou", "E5"), (2, 1, "", "ou", "D5"), (3, 3, "", "ou", "C5"), (6, 1, "n", "a", "A4"),
              (7, 1, "n", "a", "C5"), (8, 2, "l", "ai", "D5"), (10, 2, "t", "u", "E5"), (12, 1, "n", "ai", "G5"),
              (13, 3, "t", "ou", "E5")], 110, voice="soprano", fshift=1.0, air=0.16)
    write(out / "hook.wav", v)
    write(out / "oh.wav", sing([(0, 3, "", "ou", "C5")], 110, steps=3.4, voice="soprano", vib=False, air=0.12))
    return ["hook=" + str(out / "hook.wav") + LOOP, "oh=" + str(out / "oh.wav") + ":72"]


def bank_hiphop(B, out):
    """the funky drummer (bar 1, 103 BPM: at 90 slower and lower), a horn stab on Bb, and "ah" for the scratch is
    the factory one"""
    write(out / "funky.wav", bar_of(B / "funkydrummer.wav", 2))
    write(out / "horn.wav", horn())
    return ["funky=" + str(out / "funky.wav") + LOOP, "horn=" + str(out / "horn.wav") + ":58"]


BANKS = {"jungle": bank_jungle, "garage": bank_garage, "hyperpop": bank_hyperpop, "house": bank_house,
         "pop": bank_pop, "hiphop": bank_hiphop}


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    B, O = Path(sys.argv[1]), Path(sys.argv[2])
    songs = sys.argv[3:] or list(BANKS)
    for s in songs:
        if s not in BANKS:                           # (ambient, stress: no samples of their own)
            continue
        d = O / s
        d.mkdir(parents=True, exist_ok=True)
        specs = BANKS[s](B, d)
        r = subprocess.run([sys.executable, str(TOOL), "build", str(O / f"{s}.bin")] + specs)
        if r.returncode:
            sys.exit(f"{s}: the bank did not build")


if __name__ == "__main__":
    main()
