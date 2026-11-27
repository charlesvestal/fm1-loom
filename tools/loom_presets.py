#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""LOOM-8 preset packs: user presets in a .syx file, sent into U1..U64 over USB-MIDI.

  tools/loom_presets.py make PACK.syx SOUND.xy [SOUND.xy ...] [--slot N]
        each SOUND.xy (the host simulator's lines for track 2: knob ..., a sound built from LOOM-8's defaults) saved
        as a user preset, named after its file (or a "# name: ..." line), into the pack: as many as you like (a
        library: the presets page, app/web/presets, places them on the FM-1's slots); send takes them to U(N) on
  tools/loom_presets.py list PACK.syx         each preset and kit in a pack: its slot, its name, its presets format
  tools/loom_presets.py export PACK.syx       the FM-1's user presets and kits as a pack (USER_GET, KIT_GET)
  tools/loom_presets.py frombackup BACKUP.loom8 PACK.syx
                                              a backup's user presets and kits as a pack (any backup: LOOM-8 makes an
                                              older format its own when it takes them)
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


CHORD = ("C3", "E3", "G3", "C4", "E4", "G4")         # (two octaves, as a chord is played: the user's clipping)
LEVEL_DB = -28.0                                     # a chord's peak (the factory pads': glass -30, octave -29)


def chord_peak(lines, track=2):
    """a sound's peak (dBFS, the simulator's output) on CHORD at full velocity"""
    import math
    import wave
    with tempfile.TemporaryDirectory() as d:
        sc = ["wait 2500", *lines, "wait 300", "wav take.wav"] + [f"play {track} {n} on 127" for n in CHORD]
        sc += ["wait 1200"] + [f"play {track} {n} off" for n in CHORD] + ["wait 500", "wavstop"]
        (Path(d) / "s.xy").write_text("\n".join(sc) + "\n")
        subprocess.run([str(host()), str(Path(d) / "s.xy"), d], capture_output=True, env={**os.environ, "LOOM_LOAD": "0"})
        w = wave.open(str(Path(d) / "take.wav"))
        n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
        v = struct.unpack(f"<{n * ch}{'h' if sw == 2 else 'i'}", w.readframes(n))
    return 20.0 * math.log10(max(abs(x) for x in v) / (32767 if sw == 2 else 2 ** 31 - 1) + 1e-9)


def level_fit(lines, track=2, target=LEVEL_DB):
    """the sound's lines with its volume set so a two-octave chord peaks at target: measured 12 dB down (clear of the
    output's ceiling), the volume a square law (synth.c: gain = volume^2)"""
    vol = 100
    for ln in lines:
        w = ln.split()
        if len(w) == 4 and w[0] == "knob" and w[2] == "volume":
            vol = int(w[3])
    low = max(1, round(vol / 2))                     # (-12 dB: 20 log10 (1/2)^2)
    pk = chord_peak([ln for ln in lines if not ln.endswith(f" volume {vol}")] + [f"knob {track} volume {low}"], track)
    pk += 40.0 * __import__("math").log10(vol / low)
    new = max(1, min(127, round(vol * 10.0 ** ((target - pk) / 40.0))))
    return [ln for ln in lines if not ln.endswith(f" volume {vol}")] + [f"knob {track} volume {new}"], pk, new


KIT_PUT, USER_GET, KIT_GET, KIT_NAMES = 0x1F, 0x20, 0x21, 0x22
NUSER, NKITS = 64, 8
KIT_LEN, KIT_LEN16 = 321, 315


def preset_len(fmt):
    """a user preset's bytes in presets format fmt (app/project.c preset_len)"""
    return (79 if fmt >= 17 else 69 if fmt >= 16 else 64 if fmt >= 15 else 63 if fmt >= 14 else 59 if fmt >= 13
            else 55 if fmt >= 12 else 53 if fmt >= 9 else 52)


def frame(slot, fmt, data, kind=USER_PUT):
    """one USER_PUT (a preset into slot 0..63) or KIT_PUT (a kit into 0..7) message"""
    return bytes([0xF0, *HDR, kind, slot & 127, fmt & 127, fmt >> 7 & 127, *pack7(data), 0xF7])


def frames(b, kinds=(USER_PUT,)):
    """a pack's messages of these kinds: [(slot, fmt, packed data)] ([(kind, slot, fmt, packed)] for several)"""
    out, i = [], 0
    while (i := b.find(0xF0, i)) >= 0:
        j = b.find(0xF7, i)
        if j < 0:
            break
        m = b[i + 1:j]
        if list(m[:3]) == HDR and len(m) > 7 and m[3] in kinds:
            out.append((m[4], m[5] | m[6] << 7, m[7:]) if len(kinds) == 1 else (m[3], m[4], m[5] | m[6] << 7, m[7:]))
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


def backup_items(b):
    """a LOOM-8 backup (.loom8, any version) -> [(kind, fmt, bytes)]: its user presets (OBJ_UP0, OBJ_UP1 from presets
    format 19; the presets object's 16 before) and its kits (the presets object's: KIT_LEN from 17, KIT_LEN16 from 11),
    each as it was saved, in its format (LOOM-8 makes an older one its own when it takes it)"""
    if b[:4] != b"LM8B":
        raise ValueError("not a LOOM-8 backup")
    n, pos, objs = struct.unpack("<H", b[6:8])[0], 16, {}
    for _ in range(n):
        oid, ln = struct.unpack("<HH", b[pos:pos + 4])
        objs[oid] = b[pos + 8:pos + 8 + ln]
        pos += 8 + ln
    out = []
    pr = objs.get(1, b"")
    pfmt = struct.unpack("<I", pr[:4])[0] if len(pr) >= 4 else 0
    if 6 <= pfmt <= 18:                                  # (the 16 presets before 19)
        pl = preset_len(pfmt)
        out += [(USER_PUT, pfmt, pr[4 + k * pl:4 + (k + 1) * pl]) for k in range(16) if len(pr) >= 4 + (k + 1) * pl]
    for oid in (23, 24):                                 # (OBJ_UP0, OBJ_UP1)
        u = objs.get(oid, b"")
        if len(u) >= 4:
            f = struct.unpack("<I", u[:4])[0]
            pl = preset_len(f)
            out += [(USER_PUT, f, u[4 + k * pl:4 + (k + 1) * pl]) for k in range(32) if len(u) >= 4 + (k + 1) * pl]
    kat, kl = (4, KIT_LEN) if pfmt >= 19 else (4 + 16 * 79, KIT_LEN) if pfmt >= 17 else \
              (4 + 16 * preset_len(pfmt), KIT_LEN16) if pfmt >= 11 else (0, 0)
    for k in range(NKITS if kat else 0):
        img = pr[kat + k * kl:kat + (k + 1) * kl]
        if len(img) == kl:
            out.append((KIT_PUT, pfmt, img))
    return [(kind, f, d) for kind, f, d in out if d[:1] not in (b"", b"\0")]


def export(link):
    """the FM-1's user presets and kits as a pack's messages"""
    out = bytearray()
    for kind, get, n in ((USER_PUT, USER_GET, NUSER), (KIT_PUT, KIT_GET, NKITS)):
        for s in range(n):
            r = link.req(get, [s], 3.0)
            if len(r) > 4 and r[1] == 0:
                out += frame(s, r[2] | r[3] << 7, unpack7(r[4:]), kind)
    return bytes(out)


def main():
    a = sys.argv[1:]
    slot = 1
    if "--slot" in a:
        i = a.index("--slot")
        slot = int(a[i + 1])
        del a[i:i + 2]
    if len(a) < 2 or a[0] not in ("make", "list", "send", "export", "frombackup") or not 1 <= slot <= 64:
        sys.exit(__doc__)
    if a[0] == "make":
        sounds = a[2:]
        if not sounds:
            sys.exit("loom_presets: no sounds")
        out = bytearray()
        for k, f in enumerate(sounds):
            text = Path(f).read_text().splitlines()
            name = next((ln.split(":", 1)[1].strip() for ln in text if ln.lower().startswith("# name:")), Path(f).stem)
            fmt, data = preset_from_lines(name, [ln for ln in text if ln.strip() and not ln.startswith("#")])
            out += frame((slot - 1 + k) & 127, fmt, data)   # (the slot a send without --slot uses; a library of
            print(f"{k + 1:3d} {name_of(data)}")             # more than 64: the presets page places them)
        Path(a[1]).write_bytes(bytes(out))
        print(f"{a[1]}: {len(sounds)} presets")
        return
    if a[0] == "export":
        pk = export(Link())
        Path(a[1]).write_bytes(pk)
        fr = frames(pk, (USER_PUT, KIT_PUT))
        print(f"{a[1]}: {sum(f[0] == USER_PUT for f in fr)} presets, {sum(f[0] == KIT_PUT for f in fr)} kits")
        return
    if a[0] == "frombackup":
        items = backup_items(Path(a[1]).read_bytes())
        counts = {USER_PUT: 0, KIT_PUT: 0}
        out = bytearray()
        for kind, f, d in items:
            out += frame(counts[kind] & 127, f, d, kind)
            counts[kind] += 1
        Path(a[2]).write_bytes(bytes(out))
        print(f"{a[2]}: {counts[USER_PUT]} presets, {counts[KIT_PUT]} kits")
        return
    fr = frames(Path(a[1]).read_bytes(), (USER_PUT, KIT_PUT))
    if not fr:
        sys.exit(f"loom_presets: nothing in {a[1]}")
    if a[0] == "list":
        for kind, s, fmt, p in fr:
            print(f"{'U' if kind == USER_PUT else 'K'}{s + 1:<3} {name_of(unpack7(p)):12s} presets format {fmt}")
        return
    link, bad = Link(), 0
    pre = [f for f in fr if f[0] == USER_PUT]
    first = pre[0][1] if pre and "--slot" not in sys.argv else slot - 1
    for kind, s, fmt, p in fr:
        k = pre.index((kind, s, fmt, p)) if kind == USER_PUT else 0
        to = (first + k if "--slot" in sys.argv else s) if kind == USER_PUT else s
        if to > (63 if kind == USER_PUT else 7):
            print(f"{name_of(unpack7(p))}: no slot past {'U64' if kind == USER_PUT else 'K8'}")
            bad += 1
            continue
        r = link.req(kind, [to, fmt & 127, fmt >> 7 & 127, *p], 5.0)
        rc = r[1] if len(r) > 1 else 99
        print(f"{'U' if kind == USER_PUT else 'K'}{to + 1} {name_of(unpack7(p))}: "
              f"{'saved' if rc == 0 else RC.get(rc, f'rc {rc}')}")
        bad += rc != 0
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
