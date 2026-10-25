#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""How alike the presets' renders (tools/render_presets.sh) sound: per render (mono sum, frames 2048, hop
1024) a spectral shape (24 log bands 60 Hz .. 16 kHz, averaged over time, in dB normalised to the loudest
band) and an envelope (frame energy in dB normalised to the loudest frame, floor -60). The distance of two
presets is the mean absolute difference of their shapes plus half the mean absolute difference of their
envelopes, in dB. Prints, for each preset (or those matching a word given), its nearest neighbour (of all, and of those shown), the
spectral centroid (kHz, energy-weighted mean over the frames) and its movement (the centroid's standard
deviation over the frames that sound, kHz). Needs numpy.
  preset_distance.py DIR [WORD..] [--json FILE]"""
import json
import sys
import wave
from pathlib import Path

import numpy as np

NF, HOP, SR = 2048, 1024, 44100
EDGES = np.geomspace(60.0, 16000.0, 25)


def load(path):
    with wave.open(str(path), "rb") as w:
        n, ch = w.getnframes(), w.getnchannels()
        a = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return a.reshape(-1, ch).mean(axis=1)


def features(path):
    x = load(path)
    win = np.hanning(NF)
    f = np.fft.rfftfreq(NF, 1.0 / SR)
    idx = [(f >= EDGES[i]) & (f < EDGES[i + 1]) for i in range(24)]
    bands, env, cents, ws = [], [], [], []
    for s in range(0, len(x) - NF, HOP):
        p = np.abs(np.fft.rfft(x[s:s + NF] * win)) ** 2
        bands.append([float(np.sum(p[m])) for m in idx])
        e = float(np.sum(p))
        env.append(e)
        if e > 1e-9:
            cents.append(float(np.sum(f * p) / e))
            ws.append(e)
    b = np.mean(np.array(bands), axis=0) + 1e-20
    shape = 10.0 * np.log10(b / np.max(b))
    env = np.array(env) + 1e-20
    envdb = np.maximum(10.0 * np.log10(env / np.max(env)), -60.0)
    cents, ws = np.array(cents), np.array(ws)
    cen = float(np.sum(cents * ws) / np.sum(ws)) if len(ws) else 0.0
    loud = ws > np.max(ws) * 1e-3 if len(ws) else ws
    mov = float(np.std(cents[loud])) if np.any(loud) else 0.0
    return shape, envdb, cen / 1000.0, mov / 1000.0


def dist(a, b):
    n = min(len(a[1]), len(b[1]))
    return float(np.mean(np.abs(a[0] - b[0])) + 0.5 * np.mean(np.abs(a[1][:n] - b[1][:n])))


def main():
    args = sys.argv[1:]
    js = None
    if "--json" in args:
        i = args.index("--json")
        js = args[i + 1]
        del args[i:i + 2]
    d = Path(args[0])
    words = args[1:]
    names = sorted(p.stem for p in d.glob("[0-9]*-*.wav"))
    feats = {n: features(d / (n + ".wav")) for n in names}
    show = [n for n in names if not words or any(w in n for w in words)]
    out = {}
    print("%-26s %-26s %5s  %-26s %5s %5s %5s" % ("preset", "nearest of all", "dist", "nearest of these", "dist",
                                                   "cen", "move"))
    for n in show:
        best = min(((dist(feats[n], feats[m]), m) for m in names if m != n))
        near = min(((dist(feats[n], feats[m]), m) for m in show if m != n), default=(0.0, "-"))
        out[n] = {"nearest": best[1], "dist": best[0], "nearest_shown": near[1], "dist_shown": near[0],
                  "centroid": feats[n][2], "movement": feats[n][3]}
        print("%-26s %-26s %5.2f  %-26s %5.2f %5.2f %5.2f" % (n, best[1], best[0], near[1], near[0], feats[n][2],
                                                            feats[n][3]))
    if js:
        Path(js).write_text(json.dumps(out, indent=1))


if __name__ == "__main__":
    main()
