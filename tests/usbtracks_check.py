#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LOOM-8's USB audio input on ten channels, as tests/scenarios/usbtracks.xy recorded it: the master on 1 and 2, the
tracks on 3..10. With a beat on T1 and a melody on T2: their channels sound and the other six are silent; T2 panned hard
left keeps its level in its own channel (before its pan) while the master's right side loses it; T1 muted, its channel
is silent and T2's unchanged.
  tests/usbtracks_check.py DIR"""
import struct
import sys
import wave
from math import log10, sqrt


def channels(path):
    w = wave.open(path)
    assert w.getnchannels() == 10 and w.getsampwidth() == 2 and w.getframerate() == 44100, path
    n = w.getnframes()
    s = struct.unpack(f"<{n * 10}h", w.readframes(n))
    return [s[c::10] for c in range(10)]


def rms(x):
    return sqrt(sum(v * v for v in x) / max(1, len(x)))


def db(x):
    return 20 * log10(max(rms(x), 1e-9) / 32768)


def main(d):
    both, panned, muted = (channels(f"{d}/usb_{k}.wav") for k in ("both", "panned", "muted"))
    lv = lambda chs: [round(db(c), 1) for c in chs]
    print("usbtracks: dBFS by channel (master L R, T1..T8)")
    for name, chs in (("both", both), ("panned", panned), ("muted", muted)):
        print(f"  {name:7s}", lv(chs))
    fails = []
    if not (db(both[2]) > -40 and db(both[3]) > -40):
        fails.append("T1 and T2 should sound")
    if any(rms(c) for c in both[4:]):
        fails.append("T3..T8 should be silent")
    if abs(db(panned[3]) - db(both[3])) > 1.0:
        fails.append("T2's channel should keep its level panned (before its pan)")
    if not db(panned[1]) < db(both[1]) - 1.0:
        fails.append("the master's right side should lose T2 panned left")
    if rms(muted[2]):
        fails.append("T1 muted: its channel silent")
    if abs(db(muted[3]) - db(both[3])) > 1.0:
        fails.append("T1 muted: T2's channel unchanged")
    for f in fails:
        print("  FAIL", f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
