#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""LOOM-8's factory closed hat (app/samples/hat.wav), made here: six square waves at inharmonic pitches (the way the
analogue drum machines made their metal), a little noise, a band-pass round 6 kHz and a high-pass, and a fast
decay, cut 60 dB under its peak with its last 10 ms faded. In the bank at 22.05 kHz, as the hat before it (a render
of 9W9's, whose hat is a sample from ER-99, GPL-3.0: not relicensable with the rest). Run once; the result is
committed.

  tools/make_hat.py
"""
import struct
from pathlib import Path

import numpy as np

RATE = 44100           # made at 44.1 kHz, kept at 22.05 (tools/gen_samples.py resamples)
DST = Path(__file__).resolve().parents[1] / "app" / "samples" / "hat.wav"
PITCHES = [263.0, 400.0, 421.0, 474.0, 587.0, 845.0]   # inharmonic: no two near a simple ratio
LENGTH, DECAY, FADE = 0.12, 0.010, 0.010


def biquad(x, b, a):
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def bandpass(x, f, q):
    w = 2 * np.pi * f / RATE
    al = np.sin(w) / (2 * q)
    a0 = 1 + al
    return biquad(x, [al / a0, 0.0, -al / a0], [1.0, -2 * np.cos(w) / a0, (1 - al) / a0])


def highpass(x, f, q=0.707):
    w = 2 * np.pi * f / RATE
    al, c = np.sin(w) / (2 * q), np.cos(w)
    a0 = 1 + al
    return biquad(x, [(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0], [1.0, -2 * c / a0, (1 - al) / a0])


def make():
    t = np.arange(int(LENGTH * RATE)) / RATE
    metal = sum(np.sign(np.sin(2 * np.pi * p * t + k)) for k, p in enumerate(PITCHES)) / len(PITCHES)
    noise = np.random.default_rng(9).uniform(-1, 1, len(t))
    x = 0.8 * metal + 0.35 * noise
    x = bandpass(x, 6000.0, 0.8) + 0.3 * bandpass(x, 9000.0, 1.2)
    x = highpass(x, 2500.0)
    env = np.exp(-t / DECAY) * np.minimum(1.0, t / 0.0004)      # (a 0.4 ms attack: no click)
    x *= env
    pk = np.max(np.abs(x))
    tail = np.maximum.accumulate(np.abs(x[::-1]))[::-1]
    n = int(np.argmax(tail < pk * 10 ** (-60 / 20))) or len(x)
    x = x[:n].copy()
    f = int(FADE * RATE)
    x[-f:] *= np.linspace(1.0, 0.0, f) ** 2
    return x / np.max(np.abs(x))


def write(path, x):
    s = np.clip(np.round(x * 30000), -32768, 32767).astype("<i2").tobytes()
    head = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16)
    body = head + b"data" + struct.pack("<I", len(s)) + s
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)


if __name__ == "__main__":
    x = make()
    write(DST, x)
    print(f"hat: {len(x)} samples at {RATE} Hz ({len(x) / RATE:.3f} s)")
