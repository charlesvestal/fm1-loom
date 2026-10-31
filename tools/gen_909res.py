#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LOOM-8's resource region (fm1-base's device/res.c; fm1.toml resources): the 909 cymbals, ER-99's hat, ride and
crash samples as 9W9 ships them (app/samples/909/: hh.wav, ride.wav, crash.wav, GPL-3.0), 44.1 kHz, block companded
to 5 bits a sample: blocks of 32 samples, each an exponent byte (the block's step, 2^(e/2)) and the 32 codes, 5 bits
each, packed low bit first into 20 bytes (21 bytes a block). On the sample's peak the step is the smallest that
holds it, so a quiet tail keeps its detail; about 25 dB of signal over the error in each block, which listening could
not tell from the original or from 6 bits (2026-10-31). The firmware plays them in place (dsp/d909.c: the 909 hat,
open hat, ride and crash models); deterministic: the same bytes every run.

The payload (little-endian): u32 magic "N9S1", u16 count, u16 block length (32); count entries of u32 offset (of its
first block, from the payload's start) and u32 length (samples); the blocks; one byte of padding (the decoder reads a
byte past a block's codes).

  gen_909res.py OUT.bin [WAVDIR]      WAVDIR: also each sample decoded, as a 16-bit WAV (to listen to)
"""
import struct
import sys
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "app" / "samples" / "909"
NAMES = ["hh", "ride", "crash"]                   # (the order dsp/d909.c reads them in: D9S_HH, D9S_RIDE, D9S_CRASH)
BLOCK, BITS = 32, 5
MAGIC = 0x3153394E                                # "N9S1"
LIM = (1 << (BITS - 1)) - 1                       # codes -16 .. 15
STEP = [2.0 ** (e / 2.0) for e in range(32)]


def read16(path):
    """a mono WAV as 16-bit integers (24-bit rounded, as X0X's tools/gen_drum_samples.py)"""
    with wave.open(str(path), "rb") as w:
        if w.getnchannels() != 1 or w.getframerate() != 44100:
            raise SystemExit(f"gen_909res: {path}: mono 44.1 kHz expected")
        width, raw = w.getsampwidth(), w.readframes(w.getnframes())
    if width == 2:
        return list(struct.unpack(f"<{len(raw) // 2}h", raw))
    if width == 3:
        out = []
        for i in range(0, len(raw), 3):
            v = raw[i] | raw[i + 1] << 8 | raw[i + 2] << 16
            v -= (v & 0x800000) << 1
            out.append(max(-32768, min(32767, (v + 128) >> 8)))
        return out
    raise SystemExit(f"gen_909res: {path}: 16 or 24 bits expected")


def encode(x):
    """the blocks of x -> (bytes, decoded samples)"""
    out, dec = bytearray(), []
    for o in range(0, len(x), BLOCK):
        b = x[o:o + BLOCK] + [0] * (BLOCK - len(x[o:o + BLOCK]))
        pk = max(1, max(abs(v) for v in b))
        e = 0
        while e < 31 and pk / STEP[e] > LIM + 0.5:
            e += 1
        q = [max(-LIM - 1, min(LIM, int(round(v / STEP[e])))) for v in b]
        bits = 0
        for i, c in enumerate(q):
            bits |= (c + LIM + 1) << (i * BITS)
        out.append(e)
        out += bits.to_bytes(BLOCK * BITS // 8, "little")
        dec += [c * STEP[e] for c in q][:min(BLOCK, len(x) - o)]
    return bytes(out), dec


def payload(wavdir=None):
    head = struct.pack("<IHH", MAGIC, len(NAMES), BLOCK)
    off = len(head) + 8 * len(NAMES)
    entries, data = b"", b""
    for name in NAMES:
        x = read16(SRC / f"{name}.wav")
        blocks, dec = encode(x)
        entries += struct.pack("<II", off + len(data), len(x))
        data += blocks
        if wavdir:
            with wave.open(str(Path(wavdir) / f"{name}_909res.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(44100)
                w.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, int(round(v))))) for v in dec))
    return head + entries + data + b"\0"


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    p = payload(argv[2] if len(argv) == 3 else None)
    Path(argv[1]).parent.mkdir(parents=True, exist_ok=True)
    Path(argv[1]).write_bytes(p)
    print(f"  ok    909 cymbals: {len(p)} B (the resource region)")


if __name__ == "__main__":
    main(sys.argv)
