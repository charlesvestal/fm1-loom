#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's recorded instruments: cut from the Versilian Studios Chamber Orchestra 2 Community Edition (VSCO 2 CE,
CC0, github.com/sgossner/VSCO-2-CE) into app/samples/, mono 16-bit at the rate the bank keeps each at, a sustained
one's loop found (the end where the waveform best matches the start) and crossfaded into the file, stated in its
'smpl' chunk. tools/gen_samples.py reads them. Run once, by hand; the results are committed.

  prep_recordings.py VSCO_DIR       VSCO_DIR: a checkout of VSCO-2-CE (only the files below are read)
"""
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

OUT = Path(__file__).resolve().parents[1] / "app" / "samples"

# name: [(file, gain dB)] mixed, rate, seconds kept, loop (start, end) in seconds or None, the fade at the end
REC = {
    "piano": ([("Keys/Upright Nr1/UR1_C4_mf_RR1.wav", 0)], 16000, 1.5, (0.95, 1.5), 0.0),
    "upright": ([("Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_E1_v3_rr1.wav", 0)], 11025, 1.3, None, 0.4),
    "strings": ([("Strings/Violin Section/susVib/VlnEns_susVib_C4_v2.wav", 0),
                 ("Strings/Cello Section/susvib/susvib_C3_v3_1.wav", -4)], 16000, 1.5, (0.55, 1.5), 0.0),
    "brass": ([("Brass/Trumpet/sus/Sum_SHTrumpet_sus_D4_v1_rr1.wav", 0),
               ("Brass/Tenor Trombone/sus/tenortbn_sus_D2_v2_1.wav", -5)], 16000, 1.2, (0.45, 1.2), 0.0),
    "flute": ([("Woodwinds/Flute/susvib/LDFlute_susvib_C5_v1_1.wav", 0)], 16000, 1.2, (0.45, 1.2), 0.0),
    "orch hit": ([("Strings/Violin Section/Spic/VlnEns_Spic_C4_v2_rr1.wav", 0),
                  ("Strings/Cello Section/spic/spic_C3_v2_RR1.wav", -2),
                  ("Brass/F Horn/stac/MOHorn_stac_C3_v3_rr1.wav", -1),
                  ("Percussion/Timpani/Timpani1_Hit_v3_rr1_Sum.wav", -6)], 16000, 0.75, None, 0.3),
}
XFADE = 0.2                                      # s: the loop's crossfade


def load(vsco, f, rate):
    sr, x, _ = sio.read_any_wav(vsco / f)
    x = np.asarray(sio.resample(x, sr, rate), dtype=float)
    pk = np.max(np.abs(x))
    on = int(np.argmax(np.abs(x) > 0.05 * pk))
    return x[max(0, on - int(0.004 * rate)):]    # from 4 ms before the onset


def best_end(x, ls, le, rate):
    """the loop end within +-30 ms of le whose neighbourhood best matches the loop start's (normalised correlation
    over 20 ms)"""
    w = int(0.02 * rate)
    ref = x[ls:ls + w]
    best, bi = -2.0, le
    for e in range(le - int(0.03 * rate), min(len(x) - w, le + int(0.03 * rate))):
        seg = x[e:e + w]
        c = float(np.dot(ref, seg) / (np.linalg.norm(ref) * np.linalg.norm(seg) + 1e-12))
        if c > best:
            best, bi = c, e
    return bi, best


def write(path, x, rate, loop):
    s = np.clip(np.round(x / np.max(np.abs(x)) * 30000), -32768, 32767).astype("<i2").tobytes()
    chunks = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    if loop:
        smpl = struct.pack("<9I", 0, 0, 0, 60, 0, 0, 0, 1, 0) + struct.pack("<6I", 0, 0, loop[0], loop[1] - 1, 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(s)) + s
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    vsco = Path(sys.argv[1])
    for name, (srcs, rate, keep, loop, fade) in REC.items():
        parts = [load(vsco, f, rate) * 10 ** (g / 20) for f, g in srcs]
        n = int(keep * rate)
        x = np.zeros(n + int(0.05 * rate))
        for p in parts:
            m = min(len(p), len(x))
            x[:m] += p[:m]
        lp = None
        if loop:
            ls, le = int(loop[0] * rate), int(loop[1] * rate)
            le, c = best_end(x, ls, le, rate)
            f = int(XFADE * rate)
            w = np.linspace(0.0, 1.0, f)
            x[le - f:le] = x[le - f:le] * (1 - w) + x[ls - f:ls] * w   # the end flows into the start
            x = x[:le]
            lp = (ls, le)
            print(f"{name}: loop {ls}..{le} ({(le - ls) / rate:.2f} s), match {c:.3f}")
        else:
            x = x[:n]
            f = int(fade * rate)
            x[-f:] *= np.linspace(1.0, 0.0, f) ** 2
        write(OUT / f"{name.replace(' ', '_')}.wav", x, rate, lp)
        print(f"{name}: {len(x)} samples at {rate} Hz ({len(x) / rate:.2f} s)")


if __name__ == "__main__":
    main()
