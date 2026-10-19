#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The six demo songs as projects for the FM-1 (for tools/demos/device_load.py, the load test on the device).

  device_projects.py BREAKS_DIR [OUT_DIR]          (OUT_DIR default build/demos/device)

Three groups of three songs, each a restore's worth (the backup holds the three project slots):
  A: jungle garage hyperpop       B: house pop hiphop       C: stress (the worst case) ambient hiphop
For each group: one user sample bank with the three songs' samples (OUT_DIR/bank_A.bin, about 175 KB; B about
135 KB: the 216 KB bank holds them), each song's sample slots moved to its place in it (song 2's u1 is u3 ...);
then the host simulator runs each song's setup (tools/demos/<song>.xy up to "tap HOME", where its performance
starts), saves it into slot 1..3 (named after the song), starts a new project for the next, and writes the backup
(OUT_DIR/group_A.loom8, base/tools/fm1_backup.py's file). OUT_DIR/manifest.json lists, per song: its slot, the
host's model of the project as the FM-1 will play it (the backup restored in the simulator, the slot loaded from
HOME's list, its song from the Song page until it stops: project_model), render.sh's model with the performance
(model, if build/demos/run_<song>/log.txt is there), and what the script does live while it plays (knob moves,
throws, keys) that a project cannot hold: the FM-1 plays the project's song alone.

The banks hold classic breaks (make_banks.py): private, never committed or published (OUT_DIR is under build/).
Needs the host simulator built (./build.sh --host) and numpy + scipy (make_banks.py).
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import make_banks as MB  # noqa: E402

GROUPS = {"A": ["jungle", "garage", "hyperpop"], "B": ["house", "pop", "hiphop"], "C": ["stress", "ambient", "hiphop"]}
if os.environ.get("DEMO_GROUPS"):                    # e.g. DEMO_GROUPS="C:jungle,garage,house" (any three songs a group)
    GROUPS = {g: songs.split(",") for g, songs in (x.split(":") for x in os.environ["DEMO_GROUPS"].split())}
ORDER = ["jungle", "garage", "hyperpop", "house", "pop", "hiphop", "stress", "ambient"]   # render.sh's: make_banks'
#                                                  noise in the same order; stress and ambient: no samples of their own
NAMES = {"jungle": "jungle", "garage": "garage", "hyperpop": "hyperpop", "house": "deep_house", "pop": "pop",
         "hiphop": "boom_bap", "stress": "stress", "ambient": "ambient"}
SKIP = ("master ", "samples ")                       # the setup's lines the group sets once instead


def split(song):
    """(setup lines, live lines) of tools/demos/<song>.xy: the setup ends at its "tap HOME" (the song page, PLAY)"""
    lines = (HERE / f"{song}.xy").read_text().splitlines()
    k = next(i for i, ln in enumerate(lines) if ln.strip() == "tap HOME")
    return lines[:k], lines[k:]


def remap(lines, off):
    """a song's user samples uN to u(N + off): its place in the group's bank"""
    return [re.sub(r"\bu(\d+)\b", lambda m: f"u{int(m.group(1)) + off}", ln) if ln.startswith("knob ") else ln
            for ln in lines]


def live_actions(live):
    """what the script does while it plays, beyond waiting: [(bar, action)]"""
    out, bar, playing = [], 0.0, False
    for ln in live:
        w = ln.split()
        if not w or w[0].startswith("#"):
            continue
        if w[0] == "tap" and w[1] == "PLAY":
            playing = True
        elif w[0] == "bars":
            bar += float(w[1])
        elif playing and w[0] in ("knob", "throw", "play", "mst", "punch", "scene"):
            out.append((round(bar, 4), " ".join(w)))
    return out


def model(song):
    """render.sh's stats line for the song (its performance included), if it was rendered"""
    log = ROOT / "build" / "demos" / f"run_{song}" / "log.txt"
    if not log.exists():
        return None
    return next((parse_stats(ln) for ln in log.read_text().splitlines() if ln.startswith("stats:")), None)


PLAY_SONG = """master 4096
samples {bank}
restore {backup}
expect restore_rc 0
wait 300
listen
tap HOME
wait 300
tap SEL
wait 300
turn ALGO {turns}
wait 200
tap SEL
wait 500
said LOADED slot {slot}
turn SELECT 1
wait 300
stats reset
tap PLAY
wait 1000
said Play
wait {ms}
expect playing 0
stats
"""


def project_model(host, out, g, slot, bank, backup):
    """the host's model of what the FM-1 plays: the backup restored, the slot loaded from HOME's list (as
    device_load.py does it), its song from the Song page until it stops; no live gestures"""
    sp = out / f"play_{g}{slot}.xy"
    sp.write_text(PLAY_SONG.format(bank=bank, backup=backup, turns=slot - 1, slot=slot, ms=150000))
    r = subprocess.run([str(host), str(sp), str(out / "play")], capture_output=True, text=True,
                       env={**__import__("os").environ, "LOOM_LOAD": "0"})
    log = r.stdout + r.stderr
    (out / f"play_{g}{slot}.log").write_text(log)
    if r.returncode:
        sys.exit(f"group {g} slot {slot}: the song did not play and stop in the host (see {out / f'play_{g}{slot}.log'})")
    for ln in log.splitlines():
        if ln.startswith("stats:"):
            return parse_stats(ln)
    return None


def parse_stats(ln):
    m = re.search(r"([\d.]+) s playing, peak synth voices (\d+) \(of (\d+)\), peak drum voices (\d+), guard trips "
                  r"(\d+); model load peak (\d+) %, mean (\d+) %", ln)
    if not m:
        return None
    return {"line": ln, "seconds": float(m.group(1)), "synth_voices": int(m.group(2)), "drum_voices": int(m.group(4)),
            "guard": int(m.group(5)), "peak": int(m.group(6)), "mean": int(m.group(7))}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    B = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build" / "demos" / "device"
    out.mkdir(parents=True, exist_ok=True)
    host = ROOT / "app" / "build" / "host" / "loom_host"
    if not host.exists():
        sys.exit("device_projects: build the host simulator first (./build.sh --host)")
    specs = {}
    for s in ORDER:                                  # each song's WAVs, as make_banks.py makes them
        d = ROOT / "build" / "demos" / s
        d.mkdir(parents=True, exist_ok=True)
        specs[s] = MB.BANKS[s](B, d) if s in MB.BANKS else []
    manifest = {"groups": {}}
    for g, songs in GROUPS.items():
        bank = out / f"bank_{g}.bin"
        allspecs = [sp for s in songs for sp in specs[s]]
        print(f"== group {g}: {', '.join(songs)}")
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "loom_samples.py"), "build", str(bank)] + allspecs)
        if r.returncode:
            sys.exit(f"group {g}: the bank does not fit")
        script = [f"# group {g}: {' '.join(songs)} (made by tools/demos/device_projects.py)", "master 4096",
                  f"samples {bank}"]
        off, rows = 0, []
        for slot, s in enumerate(songs, 1):
            setup, live = split(s)
            setup = [ln for ln in setup if ln.strip() and not ln.lstrip().startswith("#") and not ln.startswith(SKIP)]
            if slot > 1:
                script.append("projnew")
            script += remap(setup, off)
            script += [f"projsave {slot} {NAMES[s]}", "wait 200"]
            rows.append({"slot": slot, "song": s, "name": NAMES[s].replace("_", " "), "user_samples_from": off + 1,
                         "model": model(s), "live": live_actions(live)})
            off += len(specs[s])
        script.append(f"backup out:group_{g}.loom8")
        sp = out / f"group_{g}.xy"
        sp.write_text("\n".join(script) + "\n")
        r = subprocess.run([str(host), str(sp), str(out)], capture_output=True, text=True)
        log = r.stdout + r.stderr
        (out / f"group_{g}.log").write_text(log)
        for ln in log.splitlines():
            if ln.startswith(("projsave", "demo", "FAIL", "line ")):
                print("  " + ln)
        if r.returncode or not (out / f"group_{g}.loom8").exists():
            sys.exit(f"group {g}: the host run failed (rc {r.returncode}); see {out / f'group_{g}.log'}")
        print(f"  backup {out / f'group_{g}.loom8'}, bank {bank}")
        for row in rows:
            row["project_model"] = project_model(host, out, g, row["slot"], bank, out / f"group_{g}.loom8")
            pm = row["project_model"]
            print(f"  slot {row['slot']} {row['song']}: as a project, " + (pm["line"][7:] if pm else "no stats"))
        manifest["groups"][g] = {"bank": str(bank), "backup": str(out / f"group_{g}.loom8"), "songs": rows}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"manifest {out / 'manifest.json'}")


if __name__ == "__main__":
    main()
