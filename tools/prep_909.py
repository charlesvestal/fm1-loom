#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's factory drum samples (kick, snare, hat, clap): renders of 9W9, the TR-909 model of the schwung family
(github.com/athousanddetails/schwung-9W9, GPL-3.0: its ER-99 engine and its hat samples), each cut where it has
fallen 60 dB under its peak, its last 10 ms faded (a sample that ends while it still sounds clicks), into
app/samples/909_*.wav at the rate the bank keeps it at. tools/gen_samples.py reads them in place of the made drums
it had (same names, same places in the bank). Run once, by hand; the results are committed.

  cc -O2 -std=c11 -Isrc/dsp -o er99_render src/tools/render.c src/dsp/er99_engine.c -lm   (in schwung-9W9)
  ./er99_render src OUT all
  tools/prep_909.py OUT
"""
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

DST = Path(__file__).resolve().parents[1] / "app" / "samples"
PICK = [("kick", "c_bd.wav", 16000), ("snare", "c_sd.wav", 22050), ("hat", "c_chh.wav", 22050),
        ("clap", "c_hc.wav", 22050)]
FADE = 0.010


def write(path, x, rate):
    s = np.clip(np.round(x / np.max(np.abs(x)) * 30000), -32768, 32767).astype("<i2").tobytes()
    head = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    body = head + b"data" + struct.pack("<I", len(s)) + s
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = Path(sys.argv[1])
    for name, f, rate in PICK:
        sr, x, _ = sio.read_any_wav(src / f)
        x = np.asarray(sio.resample(x, sr, rate), dtype=float)
        pk = np.max(np.abs(x))
        on = int(np.argmax(np.abs(x) > 0.02 * pk))
        x = x[on:]
        tail = np.maximum.accumulate(np.abs(x[::-1]))[::-1]          # the loudest from here to the end
        n = int(np.argmax(tail < pk * 10 ** (-60 / 20))) or len(x)
        x = x[:n].copy()
        f = min(int(FADE * rate), n // 4)
        x[-f:] *= np.linspace(1.0, 0.0, f) ** 2
        write(DST / f"909_{name}.wav", x, rate)
        print(f"{name}: {n} samples at {rate} Hz ({n / rate:.2f} s)")


if __name__ == "__main__":
    main()
