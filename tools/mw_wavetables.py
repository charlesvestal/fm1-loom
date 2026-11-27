#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Waldorf Microwave II/XT wavetables, from a dump of your own (waves.bin, tables.bin, table_names.txt: as Gearmulator's
xtDump writes them), as WAV wavetables for LOOM-8's jacquard (tools/loom_samples.py "wt:FILE.wav", the samples page).

  tools/mw_wavetables.py list DUMP_DIR                 the tables in the dump
  tools/mw_wavetables.py wav DUMP_DIR OUT_DIR [--tables 0,3,17] [--frames 16]
                                                       each table as OUT_DIR/NN_name.wav: FRAMES frames of 128
                                                       samples picked across its 61 positions, the key waves
                                                       among them (a 'clm ' chunk says the frame size)

The dump is Waldorf's ROM: bring your own, and keep it and what is made of it private (never committed or published
here: ~/private-stash). A table is its key waves (the positions a wave is named at) crossfaded across 61 positions, as
the Microwave plays it; a wave is 128 signed 8-bit samples, jacquard's own frame, so the waves pass unchanged (no
band-limiting past what they hold: the Microwave's own grit).
"""
import struct
import sys
from pathlib import Path

POS = 61


def load(d):
    d = Path(d)
    w = d.joinpath("waves.bin").read_bytes()
    t = d.joinpath("tables.bin").read_bytes()
    waves = [struct.unpack("<128b", w[i * 129 + 1:(i + 1) * 129]) for i in range(len(w) // 129)]
    tables = []
    for i in range(len(t) // 129):
        rec = t[i * 129:(i + 1) * 129]
        tables.append((rec[0] == 1, struct.unpack("<64H", rec[1:129])))
    names = d.joinpath("table_names.txt").read_text(errors="replace").split("\n")
    return waves, tables, names


def expand(tab, waves):
    """a table's 61 positions, each 128 floats (-1..1): its key waves, crossfaded between (render_tables.py's way);
    and the key positions"""
    keys = [(i, v) for i, v in enumerate(tab[:POS]) if v != 0xFFFF and v < len(waves)]
    out = [[0.0] * 128 for _ in range(POS)]
    if not keys:                                         # (only waves the dump does not hold: the generated ones)
        return out, []
    for (p0, w0), (p1, w1) in zip(keys, keys[1:] + [keys[-1]]):
        end = p1 if p1 > p0 else POS
        for p in range(p0, end):
            f = 0.0 if p1 == p0 else (p - p0) / (p1 - p0)
            out[p] = [((1 - f) * a + f * b) / 128.0 for a, b in zip(waves[w0], waves[w1])]
    return out, [k for k, _ in keys]


def pick(keys, n):
    """n positions across 0..60: the key waves first (each where it is named), the rest spread evenly between"""
    keys = sorted(set(keys))
    if len(keys) >= n:
        return [keys[round(k * (len(keys) - 1) / (n - 1))] for k in range(n)]
    chosen = set(keys)
    for k in range(n * 4):
        if len(chosen) >= n:
            break
        chosen.add(round(k * (POS - 1) / (n * 4 - 1)))
    return sorted(chosen)[:n] if len(chosen) > n else sorted(chosen)


def write_wav(path, frames):
    pcm = b"".join(struct.pack("<h", max(-32767, min(32767, round(v * 32767)))) for f in frames for v in f)
    clm = b"<!>128 10000000 wavetable (LOOM-8)"
    clm += b"\0" * (len(clm) & 1)
    fmt = struct.pack("<HHIIHH", 1, 1, 44100, 88200, 2, 16)
    body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"clm " + struct.pack("<I", len(clm)) + clm +
            b"data" + struct.pack("<I", len(pcm)) + pcm)
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def main():
    a = sys.argv[1:]
    opt = {}
    for flag in ("--tables", "--frames"):
        if flag in a:
            i = a.index(flag)
            opt[flag] = a[i + 1]
            del a[i:i + 2]
    if len(a) < 2 or a[0] not in ("list", "wav"):
        sys.exit(__doc__)
    waves, tables, names = load(a[1])
    ok = [i for i, (v, _) in enumerate(tables) if v and i < len(names) and names[i].strip() and
          all(32 <= ord(c) < 127 for c in names[i].strip())]   # (a table with a name: past them the dump holds junk)
    if a[0] == "list":
        for i in ok:
            _, keys = expand(tables[i][1], waves)
            print(f"{i:3d} {names[i].strip() if i < len(names) else '?':12s} " +
                  (f"{len(keys)} key waves" if keys else "(its waves not in the dump)"))
        return
    out = Path(a[2])
    out.mkdir(parents=True, exist_ok=True)
    n = int(opt.get("--frames", 16))
    sel = [int(x) for x in opt["--tables"].split(",")] if "--tables" in opt else ok
    for i in sel:
        if i not in ok:
            print(f"{i}: no table there")
            continue
        tab, keys = expand(tables[i][1], waves)
        if not keys:
            print(f"{i}: its waves are not in the dump (the Microwave's generated ones): skipped")
            continue
        pos = pick(keys, n)
        name = names[i].strip() if i < len(names) else f"table{i}"
        f = out / f"{i:02d}_{''.join(c if c.isalnum() else '_' for c in name).strip('_').lower()}.wav"
        write_wav(f, [tab[p] for p in pos])
        print(f"{f}  ({len(pos)} frames from {len(keys)} key waves)")


if __name__ == "__main__":
    main()
