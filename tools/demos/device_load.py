#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The six demo songs played on an attached FM-1, their audio load read from the Debug page while they play.

  tools/demos/device_load.py --devtest PKG [--normal PKG] [--groups A,B] [--no-flash] [--no-keep] [--every S]
                             [--songs A,B] [--record] [--quiet]

Made for a development build that answers fm1-base's restore question by itself (a TEMPORARY patch in base/: the
HELLO name says "LOOM-8 DEVTEST"); it refuses to run on any other (the restore would wait for SEL on the FM-1).
What it does:
  1. flashes --devtest PKG (unless --no-flash) and checks the FM-1 says DEVTEST;
  2. backs up the FM-1's projects and user presets into build/demos/device/before.loom8 (restored at the end,
     unless --no-keep; the user sample bank cannot be read back: it is left holding the last group's);
  3. the idle load once with the screen reader polling every 0.5 s and once asked every 5 s (its overhead);
  4. for each group (tools/demos/device_projects.py's: A jungle garage hyperpop, B house pop hiphop): uploads its
     sample bank (tools/loom_samples.py's upload, no question on the FM-1), restores its backup (fm1_backup.py),
     then for each slot loads it (HOME, SEL: LOAD PROJECT N), goes to the Song page (HOME, SELECT), PLAY,
     and on Debug reads "audio load N percent, voices N[ of M][, saws N], peak N percent, guard trips N, dropouts
     N" every --every seconds (0.5) until it says "stopped"; the crash log after each song (a new record stops the
     run: the log is printed, --normal PKG flashed);
  5. a table per song: the load's mean and peak, the most voices, the fewest saws, guard trips and dropouts during
     the song, a crash, against the host's model of the same project (manifest.json's project_model); the
     readings to build/demos/device/load_<song>.csv, the table to build/demos/device/device_load.txt;
  6. restores before.loom8, flashes --normal PKG (if given) and starts a NEW PROJECT.
--record: each song's USB audio recorded (ffmpeg, the FM-1 as a Mac audio input: FM1_AUDIO, default "LOOM-8 FM-1")
into build/demos/device/rec_<song>.wav and scanned for breaks (tools/demos/glitch_scan.py). --quiet: nothing asked
while a song plays (the Debug page read once it has played), as a player has it.
Run tools/demos/device_projects.py first (the banks, backups and manifest). Never switch Setup > CORES meanwhile.
FM1_REMOTE: fm1_remote.py (default base/tools/fm1_remote.py); needs mido + python-rtmidi.
"""
import argparse
import csv
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / "build" / "demos" / "device"
REMOTE = Path(os.environ.get("FM1_REMOTE", ROOT / "base" / "tools" / "fm1_remote.py"))
sys.path.insert(0, str(REMOTE.parent))
sys.path.insert(0, str(ROOT / "tools"))
import fm1_remote as fr  # noqa: E402
import loom_samples as ls  # noqa: E402
import perf_read as pr  # noqa: E402

PY = sys.executable
REC_DEVICE = os.environ.get("FM1_AUDIO", "LOOM-8 FM-1")   # (--record: the FM-1's name as a Mac audio input)
TOOLS = ROOT / "base" / "tools"
d = None


def connect(tries=40):
    global d
    for _ in range(tries):
        try:
            d = fr.FM1()
            return d
        except SystemExit:
            time.sleep(1.0)
    raise SystemExit("device_load: no FM-1 on USB-MIDI")


def disconnect():
    global d
    if d:
        try:
            d.out.close()
            d.inp.close()
        except Exception:
            pass
    d = None


def run(*cmds):
    d.run([str(c) for c in cmds])


def hello_name():
    """the HELLO answer's name (the screen reader's: "LOOM-8 DEVTEST")"""
    for _ in range(4):
        for _ in d.inp.iter_pending():
            pass
        d.sysex(fr.SR + [1, 1])
        for f in d.frames(fr.SR, 1.0, until=lambda f: f[:1] == b"\x01"):
            if f[:1] == b"\x01":
                return f[2:].split(b"\0")[0].decode("latin1", "replace")
    return ""


def where():
    """what the screen says (as tools/measure_load.sh): the Debug page's peak is then reset"""
    s = ""
    for _ in range(4):
        d.sysex(fr.SR + [1, 1])
        time.sleep(0.15)
        for _ in d.inp.iter_pending():
            pass
        d.sysex(fr.SR + [3])
        s = " ".join(f[2:].decode("latin1", "replace") for f in d.frames(fr.SR, 0.8, until=lambda f: f[:2] == b"\x10\x00")
                     if f and f[0] == 16)
        if "page" in s or " list." in s or "?" in s:
            return s
    return s


def crash_log():
    d.sysex(fr.LG)
    return [f.decode("latin1", "replace") for f in d.frames(fr.LG, 2.0, until=lambda f: f == b"end") if f != b"end"]


def num(text, name):
    m = re.search(r"\b%s (-?\d+)" % re.escape(name), text)
    return int(m.group(1)) if m else None


def release_all():
    d.btn = d.keys = 0                       # a frame at a time (measure_load.sh: release-all can lose its keys frame)
    for f in (d.buttons, d.keys_send, lambda: d.sysex(fr.LC + [4, 4097 & 0x7F, 4097 >> 7])):
        f()
        time.sleep(0.08)
    time.sleep(0.2)


GLO = ["Mix", "Master EQ", "Master color", "Master out", "Setup", "Debug"]   # (Debug: a debug build's, as the DEVTEST one)


def glo(page):
    s = where()
    for _ in range(4):
        if " of 6." in s and " list." not in s and "?" not in s:
            break
        run("tap", "HOME" if (" list." in s or "?" in s) else "GLO", "wait", 400)
        s = where()
    for _ in range(8):
        if s.startswith(page + ","):
            return s
        m = re.search(r"page (\d) of 6", s)
        cur = int(m.group(1)) if m else 1
        run("turn", "SELECT", 1 if cur < GLO.index(page) + 1 else -1, "wait", 300)
        s = where()
    raise SystemExit(f"device_load: no GLO page {page}: {s!r}")


def is_playing(s):
    return re.search(r"\bplaying, tempo", s) is not None


def stop():
    if is_playing(where()):
        run("tap", "PLAY", "wait", 600)


def answer_yes():
    s = where()
    if "?" in s:
        run("tap", "SEL", "wait", 700)


def arrange():
    """HOME until the Arrange page (HOME on Arrange turns to Song: then once more)"""
    for _ in range(4):
        s = where()
        if s.startswith("Arrange,") and " list." not in s and "?" not in s:
            return s
        run("tap", "HOME", "wait", 400)
    raise SystemExit(f"device_load: no Arrange page: {where()!r}")


def new_project():
    arrange()
    run("tap", "SEL", "wait", 400)
    for _ in range(12):
        if "NEW PROJECT" in where():
            break
        run("turn", "SELECT", 1, "wait", 200)
    run("tap", "SEL", "wait", 400)
    if "NEW PROJECT?" not in where():
        raise SystemExit("device_load: no NEW PROJECT? question")
    run("tap", "SEL", "wait", 800)
    run("tap", "HOME", "wait", 300)


def load_slot(slot, name):
    """HOME's SEL: LOAD PROJECT slot; then (on Arrange) SELECT: the Song page"""
    arrange()
    run("tap", "SEL", "wait", 400)
    for _ in range(8):
        s = where()
        m = re.search(r"LOAD PROJECT (\d)", s)
        for _ in range(3):                   # (a tap can be missed: the list did not open)
            if m:
                break
            run("tap", "SEL", "wait", 500)
            s = where()
            m = re.search(r"LOAD PROJECT (\d)", s)
        if not m:
            raise SystemExit(f"device_load: no LOAD PROJECT in HOME's list: {s!r}")
        if int(m.group(1)) == slot:
            break
        run("turn", "ALGO", 1, "wait", 250)
    if name.lower() not in s.lower():
        raise SystemExit(f"device_load: slot {slot} is not {name!r}: {s!r}")
    run("tap", "SEL", "wait", 700)
    answer_yes()                             # LOSE UNSAVED CHANGES?
    s = where()
    for _ in range(3):
        if s.startswith("Song,"):
            return s
        run("turn", "SELECT", 1 if s.startswith("Arrange,") else -1, "wait", 300)
        s = where()
    raise SystemExit(f"device_load: no Song page: {s!r}")


def status_read(s):
    m = re.search(r"audio load (\d+) percent, voices (\d+)( of (\d+))?", s)
    if not m:
        return None
    return {"load": int(m.group(1)), "voices": int(m.group(2)), "of": int(m.group(4)) if m.group(4) else 16,
            "saws": num(s, "saws") or 9, "peak": num(s, "peak") or 0, "guard": num(s, "trips") or 0,
            "drop": num(s, "dropouts") or 0, "playing": is_playing(s)}


def reader_overhead():
    """idle (stopped): the Debug page read every 0.5 s, and once every 5 s (between, the reader says BYE)"""
    glo("Debug")
    time.sleep(2.0)
    where()
    fast = []
    for _ in range(12):
        time.sleep(0.35)
        r = status_read(where())
        if r:
            fast.append(r)
    slow = []
    for _ in range(4):
        d.sysex(fr.SR + [2])                 # BYE: the reader stops listening
        time.sleep(5.0)
        r = status_read(where())
        if r:
            slow.append(r)
    out = {"fast_load": statistics.median(r["load"] for r in fast), "fast_peak": max(r["peak"] for r in fast),
           "slow_load": statistics.median(r["load"] for r in slow), "slow_peak": max(r["peak"] for r in slow)}
    print(f"  reader overhead, idle: every 0.5 s load {out['fast_load']} % (peak {out['fast_peak']} %); "
          f"every 5 s load {out['slow_load']} % (peak {out['slow_peak']} %)")
    return out


def flash(pkg):
    disconnect()
    print(f"  flashing {pkg}")
    r = subprocess.run([PY, str(TOOLS / "fm1_install.py"), str(pkg), "--yes"])
    if r.returncode:
        raise SystemExit(f"device_load: the install failed (rc {r.returncode})")
    time.sleep(4.0)
    connect()
    time.sleep(1.0)


def restore(path):
    disconnect()
    r = subprocess.run([PY, str(TOOLS / "fm1_backup.py"), "restore", str(path), "--yes"])
    time.sleep(2.0)
    connect()
    if r.returncode:
        raise SystemExit(f"device_load: the restore of {path} failed (rc {r.returncode})")


def upload_bank(path):
    """the bank uploaded (the reader told BYE first: its words share the FM-1's way out; a lost reply: again)"""
    d.sysex(fr.SR + [2])
    disconnect()
    time.sleep(6.0)
    for attempt in range(3):
        link = ls.Link()
        try:
            ls.upload(link, Path(path).read_bytes())
            break
        except TimeoutError as e:
            print(f"\n  upload: {e}; again")
            if attempt == 2:
                raise
            time.sleep(3.0)
        finally:
            link.o.close()
            link.i.close()
    connect()


def record_start(song):
    """the FM-1's USB audio (its own input device on the Mac) into DEV/rec_<song>.wav, as a player records it"""
    out = DEV / f"rec_{song}.wav"
    p = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation", "-i",
                          ":" + REC_DEVICE, "-c:a", "pcm_s24le", str(out)], stdin=subprocess.PIPE)
    time.sleep(1.5)                          # (the device opened before the song starts)
    return p, out


def record_stop(rec):
    p, out = rec
    try:
        p.communicate(b"q", timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
    r = subprocess.run([PY, str(Path(__file__).with_name("glitch_scan.py")), str(out)], capture_output=True, text=True)
    print("  recording " + str(out) + ": " + (r.stdout.strip() or r.stderr.strip()).replace("\n", "\n    "))
    return r.stdout.strip()


def play_song(row, every, overhead_note, record=False, quiet=False):
    slot, song, name = row["slot"], row["song"], row["name"]
    print(f"== slot {slot}: {song} ({name})" + (" (recorded)" if record else "") + (" (quiet)" if quiet else ""))
    log0 = crash_log()[:2]
    stop()
    load_slot(slot, name)
    release_all()
    rec = record_start(song) if record else None
    run("tap", "PLAY", "wait", 300)
    t0 = time.time()
    pm = row.get("project_model") or {}
    if quiet:                                # (no screen reader, no profiler while it plays: as a player hears it)
        disconnect()
        time.sleep(max(1.0, (pm.get("seconds") or 60) - 1.0))
        connect()
    glo("Debug")
    where()                                  # (the peak from here on)
    limit = (pm.get("seconds") or 90) * 1.6 + 20
    reads, stopped_n, g0, x0 = [], 0, None, None
    p_first = p_prev = prof_snap()
    p_last, worst, worst_trace, worst_t, dd_worst_vblk = p_first, None, None, None, None
    while time.time() - t0 < limit:
        time.sleep(max(0.0, every - 0.3))
        s = where()
        r = status_read(s)
        if not r:
            if not s.startswith("Debug,"):
                glo("Debug")
            continue
        r["t"] = round(time.time() - t0, 2)
        pn = prof_snap()
        if pn and p_prev:
            dd = pr.diff(p_prev, pn)
            if dd:
                for k in pr.STAGES:
                    r["st_" + k] = round(dd["stage"][k], 1)
                r["worst_all"] = round(dd["worst"]["all"])
                if r["playing"] and (worst is None or dd["worst"]["all"] > worst["all"]):
                    worst = dict(dd["worst"])
                    worst_trace = dd.get("trace")
                    dd_worst_vblk = dd.get("vblk")
                    worst_t = r["t"]
            if r["playing"]:
                p_last = pn
        p_prev = pn or p_prev
        if g0 is None:
            g0, x0 = r["guard"], r["drop"]
        reads.append(r)
        if not r["playing"] and re.search(r"\bstopped, tempo", s):
            stopped_n += 1
            if stopped_n >= 2:
                break
        else:
            stopped_n = 0
    secs = round(time.time() - t0, 1)
    rec_note = record_stop(rec) if rec else None
    playing = [r for r in reads if r["playing"]] or reads
    log = crash_log()
    for _ in range(3):                       # (an FM-1 at full load can miss the request: an empty answer is no answer)
        if log:
            break
        time.sleep(1.0)
        log = crash_log()
    crashed = bool(log) and bool(log0) and log[:2] != log0
    with open(DEV / f"load_{song}.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["t", "load", "peak", "voices", "of", "saws", "guard", "drop", "playing"] +
                           ["st_" + k for k in pr.STAGES] + ["worst_all"], extrasaction="ignore")
        w.writeheader()
        w.writerows(reads)
    res = {"song": song, "slot": slot, "seconds": secs, "reads": len(reads),
           "mean": round(statistics.mean(r["load"] for r in playing), 1) if playing else None,
           "peak": max((r["peak"] for r in reads), default=None),
           "max_load": max((r["load"] for r in playing), default=None),
           "voices": max((r["voices"] for r in reads), default=None),
           "of_min": min((r["of"] for r in reads), default=16),
           "saws": min((r["saws"] for r in reads), default=9),
           "guard": (reads[-1]["guard"] - g0) if reads else None, "drop": (reads[-1]["drop"] - x0) if reads else None,
           "ended": stopped_n >= 2, "crash": crashed, "log": log, "model": pm, "recording": rec_note}
    whole = pr.diff(p_first, p_last) if p_first and p_last else None
    if whole:
        res["stages"] = {k: round(v, 2) for k, v in whole["stage"].items()}
        res["eng"] = {h: {k: round(v, 2) for k, v in whole[h].items()} for h in ("eng0", "eng1")}
        res["drum"] = [round(v, 2) for v in whole["drum"]]
        res["worst"] = {k: round(v, 1) for k, v in (worst or {}).items()}
        print("  stages: " + pr.line(whole))
        if worst:
            print("  slowest block: " + " ".join(f"{k} {v:.0f}" for k, v in worst.items() if v >= 1 or k == "subs"))
        if dd_worst_vblk:
            res["vblk"] = dd_worst_vblk
            print("  its voices: " + pr.vblk_text(dd_worst_vblk))
        if worst_trace:
            res["trace"] = {"t": worst_t, "blocks": worst_trace}
            print(f"  the blocks up to it (read at {worst_t} s; all/synth0/synth1/seq/drum/note %, stretches, voices): "
                  + pr.trace_text(worst_trace))
    print(f"  {secs} s, {len(reads)} reads: load mean {res['mean']} %, highest read {res['max_load']} %, peak "
          f"{res['peak']} %, voices {res['voices']} (cap {res['of_min']}), saws {res['saws']}, guard +{res['guard']}, "
          f"dropouts +{res['drop']}, {'ended' if res['ended'] else 'DID NOT END'}, "
          f"{'CRASH' if crashed else 'no new crash'}")
    if not res["ended"]:
        stop()
    return res


def prof_snap():
    """the profiler's totals (tools/perf_read.py), None if the build has none"""
    try:
        return pr.snap(d)
    except TimeoutError:
        return None


def stages_table(results):
    order = ["jungle", "garage", "hyperpop", "house", "pop", "hiphop"]
    results = sorted([r for r in results if r.get("stages")], key=lambda r: order.index(r["song"]) if r["song"] in order else 99)
    cols = [k for k in pr.STAGES]
    lines = ["LOOM-8 demo songs on the FM-1: the render's stages, % of the budget, over the song (tools/perf_read.py)",
             "", f"{'song':<9} " + " ".join(f"{c:>6}" for c in cols)]
    for r in results:
        lines.append(f"{r['song']:<9} " + " ".join(f"{r['stages'][c]:>6.1f}" for c in cols))
    lines += ["", "the slowest block (% of one block's budget)", f"{'song':<9} " + " ".join(f"{c:>6}" for c in cols)]
    for r in results:
        w = r.get("worst") or {}
        lines.append(f"{r['song']:<9} " + " ".join(f"{w.get(c, 0):>6.0f}" for c in cols))
    lines += ["", "each engine (both cores, % of the budget) and the drum tracks"]
    for r in results:
        eng = {}
        for h in ("eng0", "eng1"):
            for k, v in r["eng"][h].items():
                eng[k] = eng.get(k, 0.0) + v
        lines.append(f"{r['song']:<9} " + " ".join(f"{k} {v:.1f}" for k, v in sorted(eng.items(), key=lambda kv: -kv[1])) +
                     " | drums " + " ".join(f"T{i + 1} {v:.1f}" for i, v in enumerate(r["drum"]) if v >= 0.05))
    text = "\n".join(lines)
    print("\n" + text)
    (DEV / "stages.txt").write_text(text + "\n")


def table(results, overhead):
    order = ["jungle", "garage", "hyperpop", "house", "pop", "hiphop"]
    results = sorted(results, key=lambda r: order.index(r["song"]) if r["song"] in order else 99)
    lines = ["LOOM-8 demo songs on the FM-1 (Debug page every ~0.5 s) against the host's model of the same project",
             f"screen reader idle: polled {overhead['fast_load']} % (peak {overhead['fast_peak']}), "
             f"asked every 5 s {overhead['slow_load']} % (peak {overhead['slow_peak']})" if overhead else "",
             "",
             f"{'song':<9} {'secs':>5} {'mean':>5} {'peak':>5} {'voices':>6} {'saws':>4} {'guard':>5} {'drop':>4} "
             f"{'crash':>5} | {'model mean':>10} {'model peak':>10} {'model voices':>12} {'secs':>5}"]
    for r in results:
        m = r["model"] or {}
        lines.append(f"{r['song']:<9} {r['seconds']:>5} {r['mean']!s:>5} {r['peak']!s:>5} {r['voices']!s:>6} "
                     f"{r['saws']:>4} {r['guard']!s:>5} {r['drop']!s:>4} {('YES' if r['crash'] else 'no'):>5} | "
                     f"{m.get('mean', '-')!s:>10} {m.get('peak', '-')!s:>10} "
                     f"{str(m.get('synth_voices', '-')) + '+' + str(m.get('drum_voices', '-')):>12} "
                     f"{m.get('seconds', '-')!s:>5}")
    text = "\n".join(lines)
    print("\n" + text)
    (DEV / "device_load.txt").write_text(text + "\n")
    (DEV / "device_load.json").write_text(json.dumps(results, indent=1) + "\n")
    stages_table(results)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--devtest", required=True, help="the auto-restore development package (loom-devtest.fwsc)")
    ap.add_argument("--normal", help="the normal package, flashed at the end and after a crash")
    ap.add_argument("--groups", default="A,B")
    ap.add_argument("--no-flash", action="store_true", help="the devtest build is on the FM-1 already")
    ap.add_argument("--no-keep", action="store_true", help="do not back up and put back the FM-1's own projects")
    ap.add_argument("--every", type=float, default=0.5)
    ap.add_argument("--songs", help="only these songs (e.g. hyperpop,house); the others' rows kept from the last run")
    ap.add_argument("--record", action="store_true", help="record each song's USB audio (ffmpeg) and scan it for breaks")
    ap.add_argument("--quiet", action="store_true", help="nothing asked while a song plays (the Debug page read at its "
                    "end): the load as a player has it (the reader's polling costs a few %%)")
    a = ap.parse_args()
    man = json.loads((DEV / "manifest.json").read_text())
    if a.no_flash:
        connect()
    else:
        flash(a.devtest)
    name = hello_name()
    print(f"  the FM-1 says: {name!r}")
    if "DEVTEST" not in name:
        raise SystemExit("device_load: the FM-1 is not running the devtest build (its restore would wait for SEL): "
                         "stopped")
    print("  crash log now: " + " | ".join(crash_log()[:2]))
    release_all()
    stop()
    before = DEV / "before.loom8"
    if not a.no_keep and before.exists():
        print(f"  ({before} is there from an earlier run: kept, not taken again)")
    elif not a.no_keep:
        disconnect()
        r = subprocess.run([PY, str(TOOLS / "fm1_backup.py"), "backup", str(before), "--magic", "LM8B"])
        connect()
        if r.returncode:
            raise SystemExit(f"device_load: the backup of the FM-1's own projects failed (rc {r.returncode})")
    overhead = reader_overhead()
    results = []
    if a.songs and (DEV / "device_load.json").exists():   # (the other songs' rows from the last run)
        results = json.loads((DEV / "device_load.json").read_text())
    try:
        for g in a.groups.split(","):
            grp = man["groups"][g]
            print(f"=== group {g}: bank {grp['bank']}, backup {grp['backup']}")
            rows = [r for r in grp["songs"] if not a.songs or r["song"] in a.songs.split(",")]
            if not rows:
                continue
            stop()
            upload_bank(grp["bank"])
            restore(grp["backup"])
            time.sleep(1.0)
            for row in rows:
                res = play_song(row, a.every, overhead, a.record, a.quiet)
                results = [r for r in results if r["song"] != res["song"]] + [res]
                (DEV / "device_load.json").write_text(json.dumps(results, indent=1) + "\n")
                if res["crash"]:
                    print("!! a crash was recorded:")
                    for ln in res["log"]:
                        print("   ", ln)
                    raise RuntimeError("crash")
    except RuntimeError:
        table(results, overhead)
        if a.normal:
            flash(a.normal)
        raise SystemExit("device_load: stopped after a crash")
    table(results, overhead)
    stop()
    if not a.no_keep and before.exists():
        print("  the FM-1's own projects and presets put back")
        restore(before)
    if a.normal:
        flash(a.normal)
        print(f"  the FM-1 says: {hello_name()!r}")
    new_project()
    print("  left on a new project: " + where()[:80])


if __name__ == "__main__":
    main()
