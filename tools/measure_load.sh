#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Measure LOOM-8's audio load on an attached FM-1, as the Status page reads it (GLO, SELECT to Status; the screen
# reader says "audio load N percent, voices N[ of M][, saws N], peak N percent, guard trips N, dropouts N"),
# driving the FM-1 over USB-MIDI with fm1-base's tools/fm1_remote.py (its remote control and the screen reader's
# channel). Nothing is flashed or saved.
#
#   tools/measure_load.sh [--label NAME] [--only engines|kit] [--engines NAME,NAME..] [--quick]
#
# It starts a NEW PROJECT itself (GLO, Setup, SEL: the current one is lost, unsaved) and ends on another. The cases:
#   idle                       stopped, nothing held
#   <engine> xN                N keys held (1, 3, 6, and the engine's cap where lower) on T3, its engine picked
#                              from SEL's list (its first preset), set to poly; brocade at 7, 5 and 3 saws (seven its most), cane's
#                              pluck and bell, voile's vowel and choir, gingham's chord with an arp; the sample
#                              engine looping (its one-shot piano would end)
#   kit playing                a beat written on T1's empty pattern 1 (kick, snare, hats), playing
#   kit + weft x6              the beat and six weft voices
#   kit + weft x3              the beat and three weft voices
#   kit + brocade 7 xN         the beat and brocade at seven saws (its cap is 5)
# Each case waits for the load's average to settle, then reads it four times: the load is their median, the peak
# the highest the page showed meanwhile, the voices the most, the saws the fewest; a case that holds fewer voices
# than it wanted is flagged (and tried once more). The keys are released a frame at a time (fm1_remote's
# release-all can lose its keys frame: a key then stays down, and its next press sounds nothing: the old 0-voice
# rows). The crash log is read after each block (a crash stops the run). A table is printed and appended to build/measure_load.csv with the label (the
# firmware), so builds can be compared:
#   ./build.sh && python3 base/tools/fm1_install.py app/build/loom.fwsc --yes && tools/measure_load.sh --label default
# FM1_REMOTE: the path of fm1_remote.py (default base/tools/fm1_remote.py); needs mido + python-rtmidi.
set -eu
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
REMOTE="${FM1_REMOTE:-base/tools/fm1_remote.py}"
[ -f "$REMOTE" ] || { echo "measure_load: no $REMOTE (set FM1_REMOTE)"; exit 1; }
mkdir -p build
exec "$PY" - "$REMOTE" "$@" <<'PY'
import csv, os, re, statistics, sys, time
remote = sys.argv[1]
sys.path.insert(0, os.path.dirname(os.path.abspath(remote)))
import fm1_remote as fr  # noqa: E402

args = sys.argv[2:]
def opt(name, default=None):
    return args[args.index(name) + 1] if name in args else default
label = opt("--label", time.strftime("%Y-%m-%d %H:%M"))
only = opt("--only")
quick = "--quick" in args
pick = opt("--engines")
WHITE_K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK_K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
CHORD = [WHITE_K[i] for i in (1, 3, 5, 7, 9, 11)]          # G3 B3 D4 F4 A4 C5: six keys a third apart
EG = ["weft", "stack", "wavetable", "orbit", "tines", "organ", "sync", "swarm", "kit", "sample", "slice",
      "brocade", "cane", "voile", "gingham"]                # synth.c EG_NAME, in order (the list's pick)
NAMES = {"model": {"cane": ["pluck", "bow", "reed", "flute", "bell", "drum"],
                   "voile": ["vosim", "vowel", "choir", "whisper"],
                   "gingham": ["toy", "chord", "ring", "peak", "band", "noise"]},
         "arp": ["off", "oct", "fifth", "power", "major", "minor", "maj7", "min7", "sus4"],
         "mode": ["one-shot", "loop", "reverse", "ping-pong"]}
# (case name, engine, knob settings on the Engine page [(encoder, knob, value)], voice cap)
ENGINES = [("weft", "weft", [], 16), ("stack", "stack", [], 16), ("wavetable", "wavetable", [], 16),
           ("orbit", "orbit", [], 16), ("tines", "tines", [], 16), ("organ", "organ", [], 16),
           ("sync", "sync", [], 16), ("swarm", "swarm", [], 16),
           ("sample loop", "sample", [("K4", "mode", "loop")], 16), ("slice", "slice", [], 16),
           ("brocade 7", "brocade", [("K1", "saws", 7)], 5), ("brocade 5", "brocade", [("K1", "saws", 5)], 6),
           ("brocade 3", "brocade", [("K1", "saws", 3)], 6),
           ("cane pluck", "cane", [("K1", "model", "pluck")], 4), ("cane bell", "cane", [("K1", "model", "bell")], 4),
           ("voile vowel", "voile", [("K1", "model", "vowel")], 16),
           ("voile choir", "voile", [("K1", "model", "choir")], 16),
           ("gingham chord arp", "gingham", [("K1", "model", "chord"), ("K4", "arp", "major")], 16)]
if pick:
    want = pick.split(",")
    ENGINES = [e for e in ENGINES if e[1] in want or e[0] in want]

d = fr.FM1()
rows = []


def run(*cmds):
    d.run([str(c) for c in cmds])


def where():
    """what the screen says (the screen reader's 'where am I'); the Status page's peak is then reset"""
    for _ in range(4):
        d.sysex(fr.SR + [1, 1])
        time.sleep(0.2)
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


LOG0 = None


def log_ok(block):
    """no new crash since the run began (the record survives restarts and a reflash: its first lines are compared)"""
    global LOG0
    got = crash_log()
    if LOG0 is None:
        LOG0 = got[:2]
    if got[:2] == LOG0:
        print(f"  [{block}: {'no crash recorded' if got[:1] == ['no crash recorded'] else 'no new crash'}]")
        return
    print(f"  !! {block}: the log says:")
    for g in got:
        print("     ", g)
    finish()
    raise SystemExit("measure_load: a crash was recorded: stopped (reflash, check Setup's CORES)")


def num(text, name):
    m = re.search(r"\b%s (-?\d+)" % re.escape(name), text)
    return int(m.group(1)) if m else None


def word(text, name):
    m = re.search(r"\b%s ([a-z0-9-]+)" % re.escape(name), text)
    return m.group(1) if m else None


def set_knob(enc, name, want):
    """turn a knob a detent at a time until the screen says NAME WANT (a number, or a name in NAMES)"""
    for _ in range(140):
        s = where()
        if isinstance(want, int):
            now = num(s, name)
            if now is None:
                raise SystemExit(f"measure_load: no '{name}' on the screen: {s!r}")
            if now == want:
                return
            diff = want - now                 # (big steps while far, single detents near: a knob may accelerate)
            run("turn", enc, diff if abs(diff) <= 2 else (1 if diff > 0 else -1) * min(abs(diff) // 2, 8), "wait", 150)
        else:
            now = word(s, name)
            names = NAMES[name]
            if isinstance(names, dict):       # (by the engine's names: the one holding what it reads now)
                names = next((v for v in names.values() if now in v and want in v), [])
            if now == want:
                return
            if now not in names:
                raise SystemExit(f"measure_load: '{name}' reads {now!r}: {s!r}")
            run("turn", enc, 1 if names.index(want) > names.index(now) else -1, "wait", 120)
    raise SystemExit(f"measure_load: {name} did not reach {want}")


def on_edit():
    """the Engine page (EDIT's first)"""
    for _ in range(4):
        s = where()
        if s.startswith("Engine, page 1"):
            return s
        run("tap", "HOME" if " list." in s else "EDIT", "wait", 350)
    raise SystemExit(f"measure_load: not on Engine: {where()!r}")


def track(t):
    """select track t (1..8) on its Engine page"""
    for _ in range(14):
        s = on_edit()
        m = re.search(r"\bT(\d+) ", s)
        if m and int(m.group(1)) == t:
            return
        cur = int(m.group(1)) if m else 0
        run("turn", "ALGO", 1 if cur < t else -1, "wait", 300)
    raise SystemExit(f"measure_load: could not select T{t}")


def engine(name):
    """the selected track's engine from SEL's list (it loads the engine's first sound)"""
    s = on_edit()
    if word(s, "engine") == name:
        return
    run("tap", "SEL", "wait", 400)
    for _ in range(20):
        s = where()
        now = word(s, "ENGINE")
        if now is None:
            raise SystemExit(f"measure_load: no ENGINE in the list: {s!r}")
        if now == name:
            break
        run("turn", "ALGO", 1 if EG.index(name) > EG.index(now) else -1, "wait", 150)
    run("tap", "SEL", "wait", 700)
    if word(on_edit(), "engine") != name:
        raise SystemExit(f"measure_load: the engine is not {name}: {where()!r}")


def mode_poly():
    """the track's voice mode to poly (Play page, KNOB 1)"""
    on_edit()
    run("turn", "SELECT", 1, "wait", 350)
    for _ in range(4):
        s = where()
        if not s.startswith("Play"):
            raise SystemExit(f"measure_load: not on Play: {s!r}")
        if "mode poly" in s:
            break
        run("turn", "K1", -1, "wait", 250)
    run("turn", "SELECT", -1, "wait", 350)


GLO = ["Mix", "Master EQ", "Master color", "Master out", "Setup", "Debug"]


def glo(page):
    """GLO's page PAGE ("Mix", "Master EQ", "Setup", "Status" ...)"""
    s = where()
    for _ in range(3):                       # (a tap can be missed)
        if " of 6." in s:
            break
        run("tap", "HOME" if " list." in s else "GLO", "wait", 400)
        s = where()
    for _ in range(8):
        if s.startswith(page + ","):
            return s
        m = re.search(r"page (\d) of 6", s)
        cur = int(m.group(1)) if m else 1
        run("turn", "SELECT", 1 if cur < GLO.index(page) + 1 else -1, "wait", 300)
        s = where()
    raise SystemExit(f"measure_load: no GLO page {page}: {s!r}")


def status(): return glo("Debug")


def new_project():
    glo("Setup")
    run("tap", "SEL", "wait", 400)
    for _ in range(12):
        s = where()
        if "NEW PROJECT" in s:
            break
        run("turn", "SELECT", 1, "wait", 200)
    run("tap", "SEL", "wait", 400)
    if "NEW PROJECT?" not in where():
        raise SystemExit("measure_load: no NEW PROJECT? question")
    run("tap", "SEL", "wait", 800)


def read_status(name, want=None):
    status()
    time.sleep(2.0 if not quick else 1.0)    # the load's average settles
    where()                                  # (the peak from here on)
    loads, peaks, voices, saws, s = [], [], [], [], ""
    for _ in range(4 if not quick else 2):
        time.sleep(0.6)
        s = where()
        m = re.search(r"audio load (\d+) percent, voices (\d+)", s)
        if not m:
            raise SystemExit(f"measure_load: not on Status: {s!r}")
        loads.append(int(m.group(1)))
        voices.append(int(m.group(2)))
        peaks.append(num(s, "peak") or 0)
        saws.append(num(s, "saws") or 9)
    load, v, pk, sw = statistics.median(loads), max(voices), max(peaks), min(saws)
    m = re.search(r"voices \d+ of (\d+)", s)
    of, guard, drop = int(m.group(1)) if m else None, num(s, "trips"), num(s, "dropouts")
    rows.append((name, load, pk, v, sw, of or 16, guard, drop))
    flag = f"   ! wanted {want} voices" if want is not None and v != want else ""
    print(f"  {name:<30} load {load:>5.1f} %  peak {pk:>3} %  voices {v:>2}{'' if of is None else ' of %d' % of}"
          f"{'' if sw == 9 else '  saws %d' % sw}  guard {guard} drop {drop}  (reads {loads}){flag}")
    return v


def hold(keys):
    for k in keys:
        run("key", k, "down")
    time.sleep(0.4)


def release_all(settle=True):
    """every key up; then (on Status) until no voice sounds (the release tails)"""
    d.btn = d.keys = 0                       # (fm1_remote's release-all sends its three frames back to back, and
    for f in (d.buttons, d.keys_send, lambda: d.sysex(fr.LC + [4, 4097 & 0x7F, 4097 >> 7])):   # the FM-1 can
        f()                                  # miss one: the keys then stay down, and the next press of one of
        time.sleep(0.08)                     # them is no press at all)
    time.sleep(0.2)
    if not settle:
        return
    status()
    for _ in range(20):
        if num(where(), "voices") == 0:
            return
        time.sleep(0.5)
    print("  ! voices still sounding after 10 s")


def counts(cap):
    return sorted({1, 3, 6} | ({cap} if cap < 6 else set()))


def finish():
    release_all(False)
    s = where()
    if "stopped" not in s:
        run("tap", "PLAY", "wait", 300)
    new_project()


print(f"LOOM-8 audio load ({label})")
LOG0 = crash_log()[:2]
if LOG0[:1] != ["no crash recorded"]:
    print("  (an earlier crash is on record: " + " ".join(LOG0) + ")")
release_all()
new_project()
print("  " + glo("Setup")[:90])
release_all()
read_status("idle", 0)
log_ok("idle")

if only in (None, "engines"):
    track(3)
    for name, eng, knobs, cap in ENGINES:
        engine(eng)
        mode_poly()
        for enc, k, v in knobs:
            set_knob(enc, k, v)
        for n in counts(cap) if not quick else (1, 6):
            on_edit()
            hold(CHORD[:n])
            got = read_status(f"{name} x{n}", min(n, cap))
            if got < min(n, cap) and eng not in ("slice", "cane"):
                release_all()                # once more: a key that did not land
                on_edit()
                hold(CHORD[:n])
                read_status(f"{name} x{n} (again)", min(n, cap))
            release_all()
        log_ok(name)

if only in (None, "kit"):
    track(3)
    engine("weft")
    mode_poly()
    track(1)
    run("tap", "SEQ", "wait", 400)
    s = where()
    if "steps on: none" not in s:
        print(f"  ! T1's pattern is not empty: the kit cases are skipped ({s[:80]!r})")
    else:
        for w in (0, 4, 8, 12):
            run("tapkey", WHITE_K[w], "wait", 120)
        run("tapkey", BLACK_K[1], "wait", 200)
        for w in (4, 12):
            run("tapkey", WHITE_K[w], "wait", 120)
        run("tapkey", BLACK_K[2], "wait", 200)
        for w in range(0, 16, 2):
            run("tapkey", WHITE_K[w], "wait", 120)
        run("tap", "PLAY", "wait", 1000)
        read_status("kit playing")
        log_ok("kit")
        track(3)
        hold(CHORD)
        read_status("kit + weft x6", 6 + 0)
        release_all()
        time.sleep(2.0)
        track(3)
        hold(CHORD[:3])
        read_status("kit + weft x3")
        release_all()
        log_ok("kit + weft")
        time.sleep(2.0)
        track(3)
        engine("brocade")
        mode_poly()
        set_knob("K1", "saws", 7)
        for n in (4, 5):
            on_edit()
            hold(CHORD[:n])
            read_status(f"kit + brocade 7 x{n}")
            time.sleep(3.0)
            read_status(f"kit + brocade 7 x{n}, 3 s on")
            release_all()
            time.sleep(2.0)
        log_ok("kit + brocade")
        run("tap", "PLAY", "wait", 300)      # stop

finish()
print("  " + glo("Setup")[:90])

out = os.path.join("build", "measure_load.csv")
HEAD = "label,case,load_pct,peak_pct,voices,saws,voices_max,guard,dropouts"
if os.path.exists(out) and open(out).readline().strip() != HEAD:
    os.replace(out, out[:-4] + ".old.csv")  # (the columns changed: the old rows kept beside)
new = not os.path.exists(out)
with open(out, "a", newline="") as f:
    w = csv.writer(f)
    if new:
        w.writerow(HEAD.split(","))
    for r in rows:
        w.writerow([label, *r])
print(f"wrote {len(rows)} rows to {out}")
PY
