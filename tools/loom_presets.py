#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""LOOM-8 preset packs: user presets in a .syx file, sent into U1..U16 over USB-MIDI.

  tools/loom_presets.py make PACK.syx SOUND.xy [SOUND.xy ...] [--slot N]
        each SOUND.xy (the host simulator's lines for track 2: knob ..., a sound built from LOOM-8's defaults) saved
        as a user preset, named after its file (or a "# name: ..." line), into the pack, U(N) onward (default U1)
  tools/loom_presets.py list PACK.syx         each preset in a pack: its slot, its name, its presets format
  tools/loom_presets.py send PACK.syx [--slot N]
        each preset into the FM-1's slot (--slot: the first one's, the rest after it); LOOM-8 saves each at once
        and says it (U3 SAVED, its name)

A pack is a run of LOOM-8's USER_PUT messages (app/samples.c: F0 7D 46 4C 1D slot fmt fmt data F7, the preset as
LOOM-8 saves one, 7 bits packed), so any SysEx sender plays one in: a librarian, a web page, this tool. A pack made
by one version loads in later ones (LOOM-8 makes an older presets format one now, as it does its flash's: project.c
user_preset_from); a newer one than the FM-1 knows is refused (rc 9). Importers make packs: tools/juno_import.py.
As a library: preset_from_lines(name, lines) -> (fmt, bytes); frame(slot, fmt, data); frames(pack bytes).
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from loom_samples import HDR, Link, pack7  # noqa: E402

USER_PUT = 0x1D
NAME_LEN = 11                                       # (app.h NAME_LEN 12, the last its 0)
RC = {1: "slot or size", 3: "the save failed", 5: "no sound", 9: "a newer presets format than this LOOM-8's"}


def host():
    h = ROOT / "app/build/host/loom_host"
    if not h.exists():
        sys.exit("loom_presets: build the host simulator first (./build.sh --host)")
    return h


def preset_from_lines(name, lines, track=2):
    """a sound made in the host simulator (its lines for the track, from a new project's) as a user preset:
    (presets format, its bytes as LOOM-8 saves them)"""
    nm = "".join(c if c.isprintable() else " " for c in name)[:NAME_LEN].replace(" ", "_") or "preset"
    with tempfile.TemporaryDirectory() as d:
        dump = Path(d) / "u.bin"
        script = ["wait 300", *lines, f"trkname {track} {nm}", f"usersave {track} 1", f"userdump 1 {dump}"]
        (Path(d) / "s.xy").write_text("\n".join(script) + "\n")
        r = subprocess.run([str(host()), str(Path(d) / "s.xy"), d], capture_output=True, text=True,
                           env={**os.environ, "LOOM_LOAD": "0"})
        if r.returncode or not dump.exists():
            sys.exit(f"loom_presets: the simulator failed on {name}:\n{r.stdout[-600:]}{r.stderr[-600:]}")
        b = dump.read_bytes()
    return struct.unpack("<I", b[:4])[0], b[4:]


def frame(slot, fmt, data):
    """one USER_PUT message: a preset into slot (0..15)"""
    return bytes([0xF0, *HDR, USER_PUT, slot & 127, fmt & 127, fmt >> 7 & 127, *pack7(data), 0xF7])


def frames(b):
    """a pack's messages: [(slot, fmt, packed data)]"""
    out, i = [], 0
    while (i := b.find(0xF0, i)) >= 0:
        j = b.find(0xF7, i)
        if j < 0:
            break
        m = b[i + 1:j]
        if list(m[:4]) == HDR + [USER_PUT] and len(m) > 7:
            out.append((m[4], m[5] | m[6] << 7, m[7:]))
        i = j + 1
    return out


def unpack7(a):
    out = []
    for g in range(0, len(a), 8):
        run = a[g:g + 8]
        out.extend(x | (run[0] >> (k - 1) & 1) << 7 for k, x in enumerate(run) if k)
    return bytes(out)


def name_of(data):
    return data[:NAME_LEN + 1].split(b"\0")[0].decode("latin-1")


def main():
    a = sys.argv[1:]
    slot = 1
    if "--slot" in a:
        i = a.index("--slot")
        slot = int(a[i + 1])
        del a[i:i + 2]
    if len(a) < 2 or a[0] not in ("make", "list", "send") or not 1 <= slot <= 16:
        sys.exit(__doc__)
    if a[0] == "make":
        sounds = a[2:]
        if not sounds or slot - 1 + len(sounds) > 16:
            sys.exit("loom_presets: 1 to 16 sounds, in U1..U16")
        out = bytearray()
        for k, f in enumerate(sounds):
            text = Path(f).read_text().splitlines()
            name = next((ln.split(":", 1)[1].strip() for ln in text if ln.lower().startswith("# name:")), Path(f).stem)
            fmt, data = preset_from_lines(name, [ln for ln in text if ln.strip() and not ln.startswith("#")])
            out += frame(slot - 1 + k, fmt, data)
            print(f"U{slot + k} {name_of(data)}")
        Path(a[1]).write_bytes(bytes(out))
        print(f"{a[1]}: {len(sounds)} presets")
        return
    fr = frames(Path(a[1]).read_bytes())
    if not fr:
        sys.exit(f"loom_presets: no presets in {a[1]}")
    if a[0] == "list":
        for s, fmt, p in fr:
            print(f"U{s + 1:<3} {name_of(unpack7(p)):12s} presets format {fmt}")
        return
    link, bad = Link(), 0
    first = fr[0][0] if "--slot" not in sys.argv else slot - 1
    for k, (s, fmt, p) in enumerate(fr):
        to = first + k if "--slot" in sys.argv else s
        if to > 15:
            print(f"{name_of(unpack7(p))}: no slot past U16")
            bad += 1
            continue
        r = link.req(USER_PUT, [to, fmt & 127, fmt >> 7 & 127, *p], 5.0)
        rc = r[1] if len(r) > 1 else 99
        print(f"U{to + 1} {name_of(unpack7(p))}: {'saved' if rc == 0 else RC.get(rc, f'rc {rc}')}")
        bad += rc != 0
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
