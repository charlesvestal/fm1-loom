#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LOOM-8's resource region (fm1-base's device/res.c; fm1.toml resources): what the firmware keeps out of its image,
written by the installer after an update and read in place. Its sections:
  N9S1  the 909 cymbals' samples (tools/gen_909res.py: dsp/d909.c reads them)
  LFT1  the large type faces (tools/gen_font.py LARGE, blob: app/gfx_loom.c loom_large_load)
The payload (little-endian): u32 magic "LRS1", u16 count, u16 0; count entries of u32 tag, u32 offset (from the
payload's start), u32 length; the sections, each 4-aligned. Deterministic: the same bytes every run.

  gen_res.py OUT.bin
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_909res  # noqa: E402
import gen_font  # noqa: E402

MAGIC = 0x3153524C                                # "LRS1"


def tag(s): return struct.unpack("<I", s.encode())[0]


def payload():
    sections = [("N9S1", gen_909res.payload()), ("LFT1", gen_font.blob())]
    head = 8 + 12 * len(sections)
    out, at, table = b"", head, b""
    for t, b in sections:
        b += bytes(-len(b) % 4)
        table += struct.pack("<III", tag(t), at, len(b))
        out += b
        at += len(b)
    return struct.pack("<IHH", MAGIC, len(sections), 0) + table + out


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    p = payload()
    Path(argv[1]).parent.mkdir(parents=True, exist_ok=True)
    Path(argv[1]).write_bytes(p)
    print(f"  ok    the resource region: {len(p)} B (the 909 cymbals, the large type faces)")


if __name__ == "__main__":
    main(sys.argv)
