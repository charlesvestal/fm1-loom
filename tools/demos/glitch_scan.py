#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Breaks in a recording of the FM-1 (its USB audio, tools/demos/device_load.py --record), and where they are.

  glitch_scan.py REC.wav [--block 256]

Three kinds, each listed by time (clustered within 50 ms):
  repeat  a stretch of samples exactly as they were a block earlier (--block frames, the render's half buffer): the
          render missed its deadline and the codec / USB played the old half again
  zeros   a run of exact digital zeros (32 or more samples) inside sound: a USB packet or a block lost
  hold    one frame held (16 or more equal frames, not zero) inside sound: the USB ring ran dry and the FM-1 sent its
          last frame again (fm1-base's usb.c uac_packet)
(Clicks by the size of a jump are not listed: a clean render's drum hits jump as far.) Prints one line:
"66.8 s: repeat 3 (45.21 45.80 63.90) | zeros 0 | hold 0". Needs numpy.
"""
import argparse
import sys
import wave

import numpy as np


def read(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, fs = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16)
        x = np.where(x & 0x800000, x - 0x1000000, x)
    else:
        x = np.frombuffer(raw, {2: np.int16, 4: np.int32}[sw]).astype(np.int64)
    return x.reshape(-1, ch).astype(np.int64), fs


def runs(mask, min_len):
    """starts of the runs of True at least min_len long"""
    m = np.concatenate(([False], mask, [False]))
    d = np.diff(m.astype(np.int8))
    s, e = np.flatnonzero(d == 1), np.flatnonzero(d == -1)
    return s[(e - s) >= min_len]


def cluster(times, gap=0.05):
    out = []
    for t in sorted(times):
        if not out or t - out[-1] > gap:
            out.append(t)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav")
    ap.add_argument("--block", type=int, default=256)
    a = ap.parse_args()
    x, fs = read(a.wav)
    mono = x.sum(axis=1)
    level = np.convolve(np.abs(mono), np.ones(441) / 441, mode="same")      # 10 ms
    loud = level > (np.abs(mono).max() * 1e-3 + 1)
    # repeats: the signal equal to itself a block earlier, both channels, 48 samples or more, inside sound
    B = a.block
    moving = np.concatenate(([False], np.any(x[1:] != x[:-1], axis=1)))           # (a held value repeats too)
    same = np.all(x[B:] == x[:-B], axis=1) & loud[B:] & moving[B:]
    rep = [(s + B) / fs for s in runs(same, 48)]
    # zeros: exact zeros in both channels, 32 samples or more, with sound on both sides
    z = np.all(x == 0, axis=1)
    zs = []
    for s in runs(z, 32):
        lo, hi = max(0, s - 2205), min(len(level) - 1, s + 2205)
        if level[lo] > 0 and level[hi] > 0 and loud[lo] and loud[hi]:
            zs.append(s / fs)
    # holds: one non-zero frame repeated, both channels
    held = np.concatenate(([False], np.all(x[1:] == x[:-1], axis=1) & np.any(x[1:] != 0, axis=1)))
    hs = [s / fs for s in runs(held, 15) if loud[max(0, s - 441)] and loud[min(len(loud) - 1, s + 441)]]
    parts = []
    for name, t in (("repeat", rep), ("zeros", zs), ("hold", hs)):
        c = cluster(t)
        parts.append(f"{name} {len(c)}" + (" (" + " ".join(f"{v:.2f}" for v in c[:24]) + (" ..." if len(c) > 24 else "")
                                           + ")" if c else ""))
    print(f"{len(x) / fs:.1f} s: " + " | ".join(parts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
