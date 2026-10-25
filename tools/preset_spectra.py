#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The presets' dry phrases (tools/render_presets.sh writes OUTDIR/dry/*.wav through tools/preset_scripts.c:
the voices alone, no FX, no master) measured for how they differ: per preset the peak and RMS (dBFS), the
spectral centroid (Hz, energy-weighted over the frames that sound) and a noisiness (the spectral flatness,
geometric over arithmetic mean of the power spectrum, 50 Hz .. 16 kHz, energy-weighted, in dB: about -60
for pure tones, 0 for white noise), grouped by engine, with each engine's centroid range. Needs numpy.
  preset_spectra.py OUTDIR [--json FILE]"""
import json
import sys
import wave
from pathlib import Path

import numpy as np


def load(path):
    with wave.open(str(path), "rb") as w:
        n, ch = w.getnframes(), w.getnchannels()
        a = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return a.reshape(-1, ch).mean(axis=1), 44100


def measure(path):
    x, sr = load(path)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    m = min(len(x), int(2.2 * sr))                  # the phrase: three notes and the chord
    rms = float(np.sqrt(np.mean(x[:m] ** 2))) if m else 0.0
    nf, hop = 2048, 512
    win = np.hanning(nf)
    f = np.fft.rfftfreq(nf, 1.0 / sr)
    band = (f >= 50.0) & (f <= 16000.0)
    cents, flats, ws = [], [], []
    for s in range(0, len(x) - nf, hop):
        p = np.abs(np.fft.rfft(x[s:s + nf] * win)) ** 2
        e = float(np.sum(p[band]))
        if e <= 1e-12:
            continue
        cents.append(float(np.sum(f[band] * p[band]) / e))
        pb = p[band] + 1e-20
        flats.append(float(np.exp(np.mean(np.log(pb))) / np.mean(pb)))
        ws.append(e)
    ws = np.array(ws)
    keep = ws > ws.max() * 1e-3 if len(ws) else ws > 0  # the frames within 30 dB of the loudest
    if not np.any(keep):
        return -200.0, -200.0, 0.0, -120.0
    w = ws[keep]
    cent = float(np.sum(np.array(cents)[keep] * w) / np.sum(w))
    flat = float(np.sum(np.array(flats)[keep] * w) / np.sum(w))
    db = lambda v: 20.0 * np.log10(v) if v > 0 else -200.0
    return db(peak), db(rms), cent, 10.0 * np.log10(max(flat, 1e-12))


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/presets")
    js = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    rows = {}
    for p in sorted((out / "dry").glob("*.wav")):
        num, eng, name = p.stem.split("-", 2)
        rows.setdefault(eng, []).append((name.replace("_", " "), *measure(p)))
    lines = ["%-10s %-14s %6s %6s %8s %7s" % ("engine", "preset", "peak", "rms", "centroid", "noise")]
    allr = []
    for eng, rs in rows.items():
        for r in rs:
            lines.append("%-10s %-14s %6.1f %6.1f %8.0f %7.1f" % (eng, *r))
            allr.append(r)
        c, z = [r[3] for r in rs], [r[4] for r in rs]
        lines.append("%-10s centroid %.0f .. %.0f Hz (x%.1f), noise %.1f .. %.1f dB" % (
            "", min(c), max(c), max(c) / max(min(c), 1.0), min(z), max(z)))
    r = [a[2] for a in allr]
    lines.append("dry rms: mean %.1f dBFS, %.1f .. %.1f" % (np.mean(r), min(r), max(r)))
    print("\n".join(lines))
    if js:
        Path(js).write_text(json.dumps({e: rs for e, rs in rows.items()}, indent=1))


if __name__ == "__main__":
    main()
