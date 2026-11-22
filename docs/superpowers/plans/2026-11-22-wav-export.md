# WAV Export Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A page on LOOM-8's site that takes a backup file, lets you pick a project and its song or a scene, and
saves a WAV rendered faster than real time in the browser.

**Architecture:** The emulator's `app.wasm` (fm1-base's host simulator in WebAssembly) gains LOOM-8's own exports
through fm1-base's existing `APP_WEB` hook (`app/web.c`, included by `base/web/emu/emu_web.c`): restore a backup
image, feed the sample bank through LOOM-8's own SysEx protocol (so `app/web/backup/extras.js`'s tested restore code
does it), open a slot, start its song or a scene, tell when it stopped. The app side of those (`app/render.c`, in
the app's unity build, web builds only) uses ui.c's own load and play. A Web Worker runs the wasm with a clock that
never moves (the overload guard sees 0 %), collects base's `web_render` output, and writes a 16-bit WAV. The page is
built by `tools/make_pages.py` like the samples page.

**Tech Stack:** C (gnu99, single precision), Emscripten (the existing `./build.sh --web`), plain ES modules, Node for
tests.

**User decisions (already made):**
- "b": an offline export, faster than real time (not a Record button in the emulator).
- "2": a separate page that takes a backup file (not a button on the emulator page).
- "1": the song, or one scene with a number of loops (no stems).
- WAV only, 16-bit 44.1 kHz stereo, the level LOOM-8 makes (not normalised); tails rendered to -80 dBFS for 1 s, 20 s at most.

**Depends on:** `docs/superpowers/plans/2026-11-22-glo-projects.md` Task 1 (`project_slot_info`).

---

Build and test (from `fm1-xy/`): `./build.sh --host` (simulator), `./build.sh --web` (needs `emcc`; writes
`app/build/emu/app.wasm`), `sh tests/run_tests.sh` (everything). `PYTHON=/opt/homebrew/bin/python3.14`.
Style: the files' own (short lower-case comments saying what and why). No `double` in app code. Commits `LOOM-8:
...`, no Co-Authored-By or session trailers.

### Task 1: The fixture: a project with a song, backed up, and the host's own render of it

**Goal:** a scenario that builds a small two-scene project with a song, saves it to slot 1, writes a backup with its
sample section, and records the host simulator's render of the song (the reference for Task 3).

**Files:**
- Create: `tests/scenarios/export_fixture.xy`

**Acceptance Criteria:**
- [ ] `app/build/scenarios/export_fixture/export.bkp` exists after the run
- [ ] `app/build/scenarios/export_fixture/export_ref.wav` exists, at least 4 bars at 120 BPM (8 s) long, not silent

**Verify:** `./build.sh --host && app/build/host/loom_host tests/scenarios/export_fixture.xy app/build/scenarios/export_fixture && ls -l app/build/scenarios/export_fixture/` → both files, exit 0

**Steps:**

- [ ] **Step 1: Read how `tools/demos/house.xy` writes patterns, scenes and a song** (`pat`, `hits`, `notes`,
  `scenedef`, `song`, `songloop`, `wav`, `bars`, `wavstop`; app/host.c `song_cmd` documents each) and how
  `tests/scenarios/backup.xy` writes a backup (`backup out:FILE`). Check whether `backup` writes the LSMP section;
  if it writes only the base image, the fixture's samples are the factory ones and Task 3's test skips the samples
  (note it in the scenario's comment).

- [ ] **Step 2: Write the scenario**

```
# the WAV export's fixture (app/web/export, tests/host/export_web.mjs): a two-scene project with a song (scene 1 for
# two bars, scene 2 for two, no loop) at 120 BPM in slot 1, backed up; then the song as the host renders it, the
# overload guard fed 0 % (LOOM_LOAD=0, as tools/demos/render.sh), for the page's render to be compared with
master 4096
bpm 120
hits 1 1 1 x...x...x...x...
hits 1 1 3 ....x.......x...
notes 2 1 1 4 100 45
notes 2 1 9 4 100 48
pat 1 2 16
hits 1 2 1 x.x.x.x.x.x.x.x.
scenedef 1 1 1 1 1 1 1 1 1 1
scenedef 2 2 1 1 1 1 1 1 1 1
song 1:2 2:2
songloop 0
projsave 1
backup out:export.bkp
wav export_ref.wav
tap HOME
tap SEL
row PLAY SONG
tap SEL
bars 4
wait 3000
wavstop
```

  Adjust the commands to the real syntax where Step 1 shows it differs (the `hits` / `notes` arguments, how the song
  starts from a script: `row PLAY SONG` is on the Song page's list, `song_cmd` may have its own start). Keep the
  musical content: a kick, a snare, a bass note on track 2, two scenes, a 4-bar song.

- [ ] **Step 3: Run it** (Verify line). Listen to nothing; check the WAV's length and peak with
  `"$PYTHON" tools/demos/analyze.py app/build/scenarios/export_fixture/export_ref.wav` if it prints levels.

- [ ] **Step 4: Commit**

```bash
git add tests/scenarios/export_fixture.xy
git commit -m "LOOM-8: the WAV export's fixture: a two-scene song backed up, and the host's render of it"
```

### Task 2: The render exports (C)

**Goal:** `app.wasm` exports what the export page needs: a buffer, restoring a backup image, a SysEx frame in and
its answers out, a slot's summary, opening a slot (its song length and scenes), starting the song or a scene, and
whether it still plays.

**Files:**
- Create: `app/render.c` (the app side: in the app's unity build, `#ifdef PLAT_WEB`)
- Create: `app/web.c` (the exports: included by `base/web/emu/emu_web.c` when `APP_WEB` is defined)
- Modify: `app/app.c` (include `render.c` after `ui.c`, `samples.c`, under `#ifdef PLAT_WEB`)
- Modify: `app/app.h` (`#define APP_WEB` next to `APP_SYSEX`; the `loom_render_*` prototypes under `#ifdef PLAT_WEB`)
- Create: `tests/host/export_wasm.mjs`

**Acceptance Criteria:**
- [ ] `node tests/host/export_wasm.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture/export.bkp` restores the backup (rc 0), reports slot 1 as `1200` tempo_x10 with 2 scenes and slots 2-3 empty
- [ ] opening slot 1 reports a song of 2 slots and scenes mask 3
- [ ] starting the song and rendering until `loom_x_playing()` is 0 takes 4 bars at 120 BPM (8.0 s) ± 0.1 s, with a peak above -40 dBFS
- [ ] starting scene 2 with 3 loops plays 3 bars (scene 2's track 1 pattern is 16 steps) ± 0.1 s
- [ ] `./build.sh` (the firmware) still builds: nothing of this is in the image

**Verify:** `./build.sh --host && app/build/host/loom_host tests/scenarios/export_fixture.xy app/build/scenarios/export_fixture && ./build.sh --web && node tests/host/export_wasm.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture/export.bkp` → `ok` lines, exit 0

**Steps:**

- [ ] **Step 1: Write the failing test** `tests/host/export_wasm.mjs`:

```js
// SPDX-License-Identifier: MPL-2.0
// The WAV export's exports (app/web.c, app/render.c) in Node: restore the fixture backup into the browser build, read
// the slots, open slot 1, render its song and one scene to their ends, check the lengths and that there is sound.
//   node tests/host/export_wasm.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture/export.bkp
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]), file = new Uint8Array(fs.readFileSync(process.argv[3]));
let mem, fails = 0;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } },
  { get: (t, k) => (k in t ? t[k] : () => 0) });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(4096); ex.web_boot(); ex.web_render(1024);
const check = (what, ok, got) => { console.log(`${ok ? "  ok" : "FAIL"} ${what}: ${got}`); fails += !ok; };

// the backup itself: up to the CRC after its objects (base/web/emu/parts/backup.js imageLength)
const u16 = (o) => file[o] | file[o + 1] << 8;
let pos = 16;
for (let k = 0; k < u16(6); k++) pos += 8 + u16(pos + 2);
const image = file.subarray(0, pos + 4);
new Uint8Array(mem.buffer, ex.loom_x_buf(), image.length).set(image);
const rc = ex.loom_x_restore(image.length);
check("restore", rc === 0, rc);
const slot = (s) => { const v = ex.loom_x_slot(s) >>> 0; return v ? { tempo: v & 0xFFFF, scenes: v >>> 16 & 0xFF } : null; };
const s1 = slot(0);
check("slot 1", s1 && s1.tempo === 1200 && s1.scenes === 2, JSON.stringify(s1));
check("slots 2, 3 empty", !slot(1) && !slot(2), `${slot(1)} ${slot(2)}`);
const song = ex.loom_x_open(0);
check("song slots", song === 2, song);
check("scenes", (ex.loom_x_scenes() >>> 0) === 3, ex.loom_x_scenes() >>> 0);

function renderToEnd(maxS) {
  let n = 0, peak = 0;
  while (ex.loom_x_playing() && n < maxS * 44100) {
    ex.web_render(1024);
    for (const v of new Float32Array(mem.buffer, ex.web_out_l(), 1024)) peak = Math.max(peak, Math.abs(v));
    n += 1024;
  }
  return { s: n / 44100, peak };
}
ex.loom_x_start(-1, 0);
let r = renderToEnd(30);
check("song: 4 bars at 120 (8 s)", Math.abs(r.s - 8) <= 0.1, r.s.toFixed(3));
check("song: sound", r.peak > 0.01, r.peak.toFixed(4));
ex.loom_x_open(0);
ex.loom_x_start(1, 3);
r = renderToEnd(30);
check("scene 2 three times: 3 bars (6 s)", Math.abs(r.s - 6) <= 0.1, r.s.toFixed(3));
process.exit(fails ? 1 : 0);
```

- [ ] **Step 2: Run it, see it fail** (`ex.loom_x_buf is not a function`).

- [ ] **Step 3: `app/render.c`** (the app side; same unity build as ui.c, so its statics are in reach):

```c
/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's render for the WAV export page (app/web/export; the browser build only, app/web.c exports it): a slot
 * opened as LOAD did it, then its song played once from the top, or one scene a number of times (a song of that
 * scene alone, made for the render: the wasm is thrown away after), to its end. The audio is fm1-base's web_render. */

int loom_render_open(int slot)                   /* slot 0..2 loaded: its song's slots, or -1 (empty) */
{
    if (!project_exists(slot))
        return -1;
    ui.ask_arg = (uint8_t)slot;
    do_load_proj();
    return pats.song.len;
}
uint32_t loom_render_scenes(void)                /* the scenes that exist: bit s for scene s + 1 */
{
    uint32_t m = 0;
    for (int s = 0; s < NSCENE; s++)
        m |= (uint32_t)(pats.scene[s].used != 0) << s;
    return m;
}
static int scene_bars(int sc)                    /* track 1's pattern in the scene, in whole bars (the scene's length) */
{
    int len = pats.pat[0][pats.scene[sc].pat[0] % NPATT].len;
    return len > 16 ? (len + 15) / 16 : 1;
}
int loom_render_start(int scene, int loops)      /* scene < 0: the song once; else scene (0..) loops times. Its bars */
{
    song_t *sg = &pats.song;
    int bars = 0;
    if (scene >= 0) {
        int per = scene_bars(scene % NSCENE), left = per * (loops < 1 ? 1 : loops);
        memset(sg, 0, sizeof *sg);
        while (left > 0 && sg->len < NSONG) {    /* (a slot plays SONG_BARS at most) */
            int b = left > SONG_BARS ? SONG_BARS : left;
            sg->slot[sg->len].scene = (uint8_t)(scene % NSCENE);
            sg->slot[sg->len++].rep = (uint8_t)b;
            left -= b;
        }
    }
    sg->loop = 0;                                /* (once: its end stops the transport) */
    for (int i = 0; i < sg->len; i++)
        bars += sg->slot[i].rep ? sg->slot[i].rep : 1;
    ui.page = P_SONG;                            /* as PLAY SONG: from the top */
    ui.name_slot = 0;
    if (playing())
        engine_transport(0);
    play_press();
    return bars;
}
int loom_render_playing(void) { return playing(); }
```

  Check while writing: `scene_t`'s pattern field is `pat[]` (host.c line ~437 uses `pats.scene[sc].pat[t]`);
  `tpat_t.len` is steps 1..64 (pattern.h line 101; a step a sixteenth: 16 a bar); `play_press()` on `P_SONG` with
  `ui.name_slot = 0` starts the song from slot 1 (the PLAY SONG action does exactly this). If a pattern's SCALE
  (speed) makes a step longer than a sixteenth, the scene's bars come out longer: then count bars from the song's
  own clock instead (`seq` song_pos / song_rep), not from `len`.

- [ ] **Step 4: `app/app.c`** — after `#include "samples.c"`:

```c
#ifdef PLAT_WEB
#include "render.c"                                  /* the WAV export page's render (the browser build only) */
#endif
```

  **`app/app.h`** — beside `#define APP_SYSEX`:

```c
#define APP_WEB                         /* the browser build's own exports (web.c: the WAV export page's render) */
#ifdef PLAT_WEB
int loom_render_open(int slot);
uint32_t loom_render_scenes(void);
int loom_render_start(int scene, int loops);
int loom_render_playing(void);
#endif
```

- [ ] **Step 5: `app/web.c`** (inside emu_web.c's translation unit: base's `sim_img`, `sim_restore_image`,
  `hsx_out` / `hsx_n` are in reach; the app's functions through their prototypes):

```c
/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's own exports for the browser build (app.h APP_WEB; base/web/emu/emu_web.c includes this): the WAV export
 * page's worker (app/web/export/render.js) restores a backup, sends the sample bank as the backup page does (LOOM-8's
 * SysEx, F0 7D 46 4C: samples.c), opens a slot and plays its song or a scene to the end. */
#define WEB_EXPORT __attribute__((used, visibility("default")))

static uint8_t x_buf[1024];                      /* a SysEx frame from the page (F0 .. F7) */
WEB_EXPORT uint8_t *loom_x_buf(void) { return sim_img; }          /* a backup image goes straight where restore reads it */
WEB_EXPORT uint32_t loom_x_buf_max(void) { return sizeof sim_img; }
WEB_EXPORT int loom_x_restore(uint32_t n) { return n > sizeof sim_img ? 3 : sim_restore_image(n); }
WEB_EXPORT uint8_t *loom_x_sx_buf(void) { return x_buf; }
WEB_EXPORT int loom_x_sx(uint32_t n)             /* the frame in loom_x_sx_buf to the app, as USB hands it on */
{
    if (n < 2u || n > sizeof x_buf || x_buf[0] != 0xF0u || x_buf[n - 1u] != 0xF7u)
        return -1;
    return app_sysex(x_buf + 1, n - 2u);
}
WEB_EXPORT uint8_t *loom_x_out(void) { return hsx_out; }          /* the app's answers, F0 .. F7 frames */
WEB_EXPORT uint32_t loom_x_out_len(void) { return hsx_n; }
WEB_EXPORT void loom_x_out_clear(void) { hsx_n = hsx_lost = 0; }
WEB_EXPORT uint32_t loom_x_slot(int slot)        /* a slot's tempo_x10 | scenes << 16, 0 for an empty one */
{
    uint16_t tx;
    uint8_t ns;
    return project_slot_info(slot, &tx, &ns) ? (uint32_t)tx | (uint32_t)ns << 16 : 0u;
}
WEB_EXPORT int loom_x_open(int slot) { return loom_render_open(slot); }
WEB_EXPORT uint32_t loom_x_scenes(void) { return loom_render_scenes(); }
WEB_EXPORT int loom_x_start(int scene, int loops) { return loom_render_start(scene, loops); }
WEB_EXPORT int loom_x_playing(void) { return loom_render_playing(); }
```

  If `sim_img` / `sim_restore_image` are behind `#if FM1B_BACKUP` and that isn't set in the web build, check
  `base/host/plat_host.c` around line 1828 for the condition and make the web build meet it (fm1.toml already has
  `backup = true`). If `hsx_out` isn't filled in the web build (look where `plat_sysex_send` writes in
  plat_host.c), use that function's buffer instead. Don't change fm1-base for this unless there's no other way; if
  there's none, stop and report.

- [ ] **Step 6: Build and run** (Verify line). Expected: all `ok`. If the song's length is off by a bar, look at
  when `play_press` starts (a count-in? `proj.rsv[0]`, the count-in, should be off in the fixture: set it off in
  `loom_render_start` if it isn't) and at the tail of the last bar.

- [ ] **Step 7: Check the firmware still builds** — `./build.sh` → `ok image` line, no errors.

- [ ] **Step 8: Add the test to `tests/run_tests.sh`** after the scenarios loop (it needs the fixture), beside the
  other emu tests' `command -v emcc` guard:

```sh
    run export-wasm node tests/host/export_wasm.mjs $B/emu/app.wasm $B/scenarios/export_fixture/export.bkp
```

- [ ] **Step 9: Commit**

```bash
git add app/render.c app/web.c app/app.c app/app.h tests/host/export_wasm.mjs tests/run_tests.sh
git commit -m "LOOM-8: the browser build's render exports for the WAV export page: restore a backup, open a slot, play its song or a scene to the end"
```

### Task 3: The render worker and the WAV

**Goal:** `app/web/export/render.js`: one module, usable in a Web Worker and in Node, that takes the backup file's
bytes and the wasm, restores the projects and the sample bank, lists the slots, and renders a pick to a WAV
(`Uint8Array`), reporting progress.

**Files:**
- Create: `app/web/export/render.js` (the module), `app/web/export/worker.js` (the Worker's message loop around it)
- Create: `tests/host/export_web.mjs`
- Modify: `tests/run_tests.sh`

**Acceptance Criteria:**
- [ ] `exportSetup(wasm, file)` returns the three slots (`{slot, tempo, scenes}` or `null`) and throws `"That file is not a LOOM-8 backup."` for a file without the LM8B magic, `"This backup is damaged."` for one cut short
- [ ] `exportRender({slot: 0, scene: -1})` gives a WAV: RIFF/WAVE, 16-bit, 44100 Hz, 2 channels, at least 8 s and at most 8 s + 20 s + 1 block, ending with at least 1 s under -80 dBFS
- [ ] its first 8 s' RMS is within 1 dB of `export_ref.wav`'s first 8 s from the start of sound (the host's render of the same song)
- [ ] the samples: a backup with an LSMP section goes through `extras.js`'s restore (its `restore(link, data)`) against the wasm, and the bank's count after it equals the section's

**Verify:** `node tests/host/export_web.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture` → `ok` lines, exit 0

**Steps:**

- [ ] **Step 1: Write the failing test** `tests/host/export_web.mjs`:

```js
// SPDX-License-Identifier: MPL-2.0
// The WAV export page's render (app/web/export/render.js) in Node: the fixture's backup set up, its song rendered to
// a WAV, checked against the host simulator's render of the same song (tests/scenarios/export_fixture.xy); bad files
// refused in plain words.
//   node tests/host/export_web.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture
import fs from "fs";
import { exportSetup, exportRender } from "../../app/web/export/render.js";
const wasm = fs.readFileSync(process.argv[2]), dir = process.argv[3];
const file = new Uint8Array(fs.readFileSync(`${dir}/export.bkp`));
let fails = 0;
const check = (what, ok, got) => { console.log(`${ok ? "  ok" : "FAIL"} ${what}: ${got}`); fails += !ok; };
const pcm = (wav) => { const v = new DataView(wav.buffer, wav.byteOffset); let o = 12;
  while (o < wav.length && String.fromCharCode(...wav.subarray(o, o + 4)) !== "data") o += 8 + v.getUint32(o + 4, true);
  return new Int16Array(wav.buffer.slice(wav.byteOffset + o + 8, wav.byteOffset + o + 8 + v.getUint32(o + 4, true))); };
const rms = (s, from, n) => { let a = 0; for (let i = from; i < from + n; i++) a += (s[i] / 32768) ** 2; return Math.sqrt(a / n); };
const first = (s) => { let i = 0; while (i < s.length && Math.abs(s[i]) < 33) i++; return i; };

for (const [what, bytes, msg] of [["not a backup", new Uint8Array(64), "That file is not a LOOM-8 backup."],
                                  ["cut short", file.subarray(0, 40), "This backup is damaged."]]) {
  try { await exportSetup(wasm, bytes); check(what, false, "accepted"); }
  catch (e) { check(what, e.message === msg, e.message); }
}
const slots = await exportSetup(wasm, file);
check("slots", slots[0] && slots[0].tempo === 1200 && slots[0].scenes === 2 && !slots[1] && !slots[2], JSON.stringify(slots));
let last = 0;
const wav = await exportRender({ slot: 0, scene: -1 }, (p) => { last = p; });
const v = new DataView(wav.buffer, wav.byteOffset);
check("RIFF WAVE", String.fromCharCode(...wav.subarray(0, 4)) === "RIFF" && String.fromCharCode(...wav.subarray(8, 12)) === "WAVE", "");
check("16-bit 44.1 kHz stereo", v.getUint16(22, true) === 2 && v.getUint32(24, true) === 44100 && v.getUint16(34, true) === 16, "");
const s = pcm(wav), secs = s.length / 2 / 44100;
check("length: the song, then its tail", secs >= 8 && secs <= 8 + 20 + 0.05, secs.toFixed(2));
check("ends quiet (the last second under -80 dBFS)", rms(s, s.length - 88200, 88200) < 1e-4, rms(s, s.length - 88200, 88200).toExponential(2));
check("progress reached 1", last === 1, last);
const ref = pcm(new Uint8Array(fs.readFileSync(`${dir}/export_ref.wav`)));
const a = rms(s, first(s), 8 * 88200 - 1), b = rms(ref, first(ref), 8 * 88200 - 1), db = 20 * Math.log10(a / b);
check("level: the host's render within 1 dB", Math.abs(db) <= 1, `${db.toFixed(2)} dB`);
process.exit(fails ? 1 : 0);
```

  (If `export_ref.wav` is 24-bit or float, read it by its fmt chunk; the host's WAV writer is base/host/plat_host.c
  `wav`. The level check compares the same 8 s from the first sound in each.)

- [ ] **Step 2: Run it, see it fail** (module not found).

- [ ] **Step 3: `app/web/export/render.js`.** The backup's own part and its sections are laid out as base's backup
  page reads them (base/web/backup/index.html lines ~245-266: after the backup's CRC, each section is a 4-letter
  tag, a u32 length, the bytes, a u32 CRC-32). The samples' restore is `extras.js`'s `LOOM_SAMPLES.restore(link,
  data)`; it is not a module (make_pages inlines it inside a function after `loom_samples.js`), so load both texts
  the way make_pages does into a function that returns `EXTRAS` — in the page, make_pages inlines them (Task 4); in
  Node, read the files. Give `render.js` an `extras` argument for that:

```js
// SPDX-License-Identifier: MPL-2.0
// LOOM-8's WAV export (the export page's worker, and tests/host/export_web.mjs): the emulator's app.wasm, a backup
// file restored into it (the projects, then the samples through LOOM-8's own SysEx, as the backup page sends them),
// a project's song or a scene played to its end faster than real time, written as a 16-bit WAV. The wasm's clock
// never moves, so its overload guard sees 0 %: every voice sounds, and a project always gives the same file.

const BACKUP_MAGIC = 0x42384D4C;                   // "LM8B" (app.h APP_BACKUP_MAGIC)
const BLOCK = 1024, RATE = 44100, TAIL_MAX = 20, QUIET = 1e-4, QUIET_S = 1;   // -80 dBFS for one second

let ex = null, mem = null, extrasFn = null;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;

/** extras: a function that returns the backup page's EXTRAS (app/web/backup/extras.js), for the samples */
export function useExtras(fn) { extrasFn = fn; }

function split(file) {                             // -> {image, sections: [{tag, data}]}; throws in plain words
  const bad = () => new Error("This backup is damaged.");
  if (file.length < 4 || u32(file, 0) !== BACKUP_MAGIC) throw new Error("That file is not a LOOM-8 backup.");
  if (file.length < 20) throw bad();
  let pos = 16;
  for (let k = 0, n = file[6] | file[7] << 8; k < n; k++) {
    if (pos + 8 > file.length) throw bad();
    pos += 8 + (file[pos + 2] | file[pos + 3] << 8);
  }
  if (pos + 4 > file.length) throw bad();
  const image = file.subarray(0, pos + 4), sections = [];
  for (let at = pos + 4; at < file.length;) {
    if (at + 8 > file.length) throw bad();
    const tag = String.fromCharCode(...file.subarray(at, at + 4)), ln = u32(file, at + 4);
    if (at + 8 + ln + 4 > file.length) throw bad();
    sections.push({ tag, data: file.subarray(at + 8, at + 8 + ln) });
    at += 8 + ln + 4;
  }
  return { image, sections };
}

async function boot(wasm) {
  const wasi = new Proxy({
    clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },   // time stands still
    fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; },
  }, { get: (t, k) => (k in t ? t[k] : () => 0) });
  const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
  ex = instance.exports; mem = ex.memory;
  if (ex._initialize) ex._initialize();
  ex.web_master(4096);
  ex.web_boot();
  ex.web_render(BLOCK);
}

// a link to the wasm for extras.js (as base's backup page gives it a USB one): send a frame, read the answers
function wasmLink() {
  const queue = [];
  const take = () => {
    const n = ex.loom_x_out_len();
    if (!n) return;
    const out = new Uint8Array(mem.buffer, ex.loom_x_out(), n).slice();
    ex.loom_x_out_clear();
    for (let i = 0; i < out.length;) { const j = out.indexOf(0xF7, i); if (j < 0) break; queue.push(out.slice(i, j + 1)); i = j + 1; }
  };
  return {
    lost: false,
    send(bytes) { new Uint8Array(mem.buffer, ex.loom_x_sx_buf(), bytes.length).set(bytes); ex.loom_x_sx(bytes.length); ex.web_render(128); take(); },
    async read(ms) { for (let k = 0; !queue.length && k < 50; k++) { ex.web_render(BLOCK); take(); } return queue.shift() || null; },
  };
}

/** the backup in file (bytes) set up in a fresh app.wasm: the three slots, {slot, tempo, scenes} or null */
export async function exportSetup(wasm, file) {
  const { image, sections } = split(file);
  await boot(wasm);
  if (image.length > ex.loom_x_buf_max()) throw new Error("This backup is damaged.");
  new Uint8Array(mem.buffer, ex.loom_x_buf(), image.length).set(image);
  const rc = ex.loom_x_restore(image.length);
  if (rc === 3) throw new Error("That file is not a backup of this LOOM-8. Update this page, or the FM-1.");
  if (rc) throw new Error("This backup is damaged.");
  const smp = sections.find((s) => s.tag === "LSMP");
  if (smp && extrasFn) {
    const lib = extrasFn().find((e) => e.tag === "LSMP");
    await lib.restore(wasmLink(), smp.data);
  }
  return [0, 1, 2].map((slot) => {
    const v = ex.loom_x_slot(slot) >>> 0;
    return v ? { slot, tempo: v & 0xFFFF, scenes: v >>> 16 & 0xFF } : null;
  });
}

/** after exportSetup: a slot's song slots and scenes (bit s: scene s + 1) */
export function exportOpen(slot) { const song = ex.loom_x_open(slot); return { song, scenes: ex.loom_x_scenes() >>> 0 }; }

/** pick {slot, scene (-1: the song), loops}: the WAV's bytes; progress(0..1) as it goes */
export async function exportRender(pick, progress = () => {}) {
  exportOpen(pick.slot);
  const bars = ex.loom_x_start(pick.scene, pick.loops || 1), tempo = (ex.loom_x_slot(pick.slot) & 0xFFFF) / 10;
  const expect = Math.max(1, bars * 4 * 60 / tempo * RATE);
  const chunks = [];
  let frames = 0, quiet = 0, tail = 0, ended = false;
  while (true) {
    ex.web_render(BLOCK);
    const l = new Float32Array(mem.buffer, ex.web_out_l(), BLOCK), r = new Float32Array(mem.buffer, ex.web_out_r(), BLOCK);
    const pcm = new Int16Array(BLOCK * 2);
    let loud = 0;
    for (let i = 0; i < BLOCK; i++) {
      const a = Math.max(-1, Math.min(1, l[i])), b = Math.max(-1, Math.min(1, r[i]));
      pcm[2 * i] = Math.round(a * 32767); pcm[2 * i + 1] = Math.round(b * 32767);
      loud = Math.max(loud, Math.abs(a), Math.abs(b));
    }
    chunks.push(pcm);
    frames += BLOCK;
    if (!ended && !ex.loom_x_playing()) ended = true;
    if (ended) {
      tail += BLOCK;
      quiet = loud < QUIET ? quiet + BLOCK : 0;
      if (quiet >= QUIET_S * RATE || tail >= TAIL_MAX * RATE) break;
    } else if (frames % (BLOCK * 32) === 0) {
      progress(Math.min(0.99, frames / expect));
      await new Promise((res) => setTimeout(res, 0));   // (the worker answers messages between bursts)
    }
  }
  progress(1);
  return wavOf(chunks, frames);
}

function wavOf(chunks, frames) {
  const data = frames * 4, out = new Uint8Array(44 + data), v = new DataView(out.buffer);
  const str = (o, s) => { for (let i = 0; i < 4; i++) out[o + i] = s.charCodeAt(i); };
  str(0, "RIFF"); v.setUint32(4, 36 + data, true); str(8, "WAVE");
  str(12, "fmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
  v.setUint32(24, RATE, true); v.setUint32(28, RATE * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true);
  str(36, "data"); v.setUint32(40, data, true);
  let o = 44;
  for (const c of chunks) { out.set(new Uint8Array(c.buffer), o); o += c.byteLength; }
  return out;
}
```

  The quiet test uses one block's peak; "1 s under -80 dBFS" is then 1 s of blocks whose peak is under 1e-4.
  The spec's "a project from a newer LOOM-8" case is `rc === 3` (another version's backup).

- [ ] **Step 4: `app/web/export/worker.js`** (the page's Worker: one message at a time):

```js
// SPDX-License-Identifier: MPL-2.0
// The export page's Worker: render.js away from the page, so the page stays responsive. Messages in:
// {type: "setup", wasm, file} -> {type: "slots", slots} | {type: "error", message};
// {type: "open", slot} -> {type: "opened", song, scenes}; {type: "render", pick} -> {type: "progress", p} ...,
// then {type: "wav", bytes} (transferred).
import { exportSetup, exportOpen, exportRender, useExtras } from "./render.js";
self.onmessage = async (e) => {
  const m = e.data;
  try {
    if (m.type === "setup") {
      if (self.LOOM_EXTRAS) useExtras(self.LOOM_EXTRAS);
      postMessage({ type: "slots", slots: await exportSetup(m.wasm, new Uint8Array(m.file)) });
    } else if (m.type === "open") {
      postMessage({ type: "opened", ...exportOpen(m.slot) });
    } else if (m.type === "render") {
      const bytes = await exportRender(m.pick, (p) => postMessage({ type: "progress", p }));
      postMessage({ type: "wav", bytes }, [bytes.buffer]);
    }
  } catch (err) {
    postMessage({ type: "error", message: err.message || String(err) });
  }
};
```

  (`self.LOOM_EXTRAS`: the page build (Task 4) puts the samples' code in front of the worker as a classic prelude,
  or turns `extras.js` into an importable module there; pick whichever make_pages makes simplest and say which in
  the commit.)

- [ ] **Step 5: The samples in the test.** In `export_web.mjs`, before `exportSetup(wasm, file)`, load the extras
  as make_pages does, and pass them:

```js
import { useExtras } from "../../app/web/export/render.js";
const strip = (t) => t.replace(/^export\s+/gm, "");
const extrasSrc = strip(fs.readFileSync("app/web/samples/loom_samples.js", "utf8")) + "\n" +
                  fs.readFileSync("app/web/backup/extras.js", "utf8");
useExtras(new Function(extrasSrc));
```

  and if the fixture's backup has an LSMP section, add a check that the wasm's bank count afterwards (SysEx 15's
  answer, as extras.js reads it) equals the section's count. Check `tools/make_pages.py` lines ~254-263 for exactly
  how it wraps these two files and do the same.

- [ ] **Step 6: Run the test** (Verify line). Expected: all `ok`.

- [ ] **Step 7: Add it to `tests/run_tests.sh`** right after `export-wasm`:

```sh
    run export-web node tests/host/export_web.mjs $B/emu/app.wasm $B/scenarios/export_fixture
```

- [ ] **Step 8: Commit**

```bash
git add app/web/export/render.js app/web/export/worker.js tests/host/export_web.mjs tests/run_tests.sh
git commit -m "LOOM-8: the WAV export's render: a backup restored into the browser build, a song or a scene played to its end, a 16-bit WAV"
```

### Task 4: The page, the site, the links, the manual

**Goal:** `export/` on LOOM-8's site: open a backup file, pick a project and its song or a scene with loops, Export
with a progress bar, the WAV saved as `loom-8-proj-N-song.wav` / `loom-8-proj-N-scene-S.wav`. Linked from the site's
index, the samples and backup pages, and the manual.

**Files:**
- Create: `app/web/export/index.html`
- Modify: `tools/make_pages.py` (build `export/`: the page, `render.js`, `worker.js`, the samples' code, and the
  emulator's `app.wasm` it fetches from `../emu/app.wasm`; add it to `SECTIONS` and the index's cards)
- Modify: `docs/MANUAL.md` (a short "Export a song as a WAV" section)
- Modify: `tests/host/emu_page_test.mjs` or a new `tests/host/export_page_test.mjs` if the site's pages are tested there

**Acceptance Criteria:**
- [ ] `tools/publish_pages.sh`'s site build (or `"$PYTHON" tools/make_pages.py OUT`) writes `OUT/export/index.html`, `render.js`, `worker.js`, and the index links to it
- [ ] the page in a browser (served from the built site): choosing `export.bkp` lists "Proj 1 · 120 bpm · 2 scenes", "Proj 2 · empty", "Proj 3 · empty"; picking Proj 1 offers "Song" and "Scene 1", "Scene 2"; Export saves `loom-8-proj-1-song.wav`
- [ ] a bad file shows the render's message in the page's alert line; nothing else changes
- [ ] the page passes fm1-reader's `tools/a11y/check.mjs` (as the other pages)
- [ ] the manual has the section, linking `../export/`

**Verify:** `"$PYTHON" tools/make_pages.py /tmp/loom-site && ls /tmp/loom-site/export/` → `index.html render.js worker.js` (+ the wasm path the page uses resolves: `ls /tmp/loom-site/emu/app.wasm`)

**Steps:**

- [ ] **Step 1: Read the samples page** (`app/web/samples/index.html`) and how `tools/make_pages.py` builds it
  (lines ~254-300: the `/*LIB*/` inlining, the tape-deck dress, the header): the export page follows it — its own
  `index.html` with a marker for the inlined extras, styled by make_pages, one header.

- [ ] **Step 2: Write `app/web/export/index.html`.** Content, in the site's voice (talk to the reader, whole
  sentences, US spelling; the web way first):

```html
<h1>Export a song</h1>
<p>Make a WAV file of a song or a scene from one of your projects. It plays in your browser, faster than real time,
   so you don't need the FM-1 for it.</p>
<ol>
  <li>Save a backup of your FM-1 on <a href="../backup/">the backup page</a>, or from <a href="../emu/">the
      emulator</a>.</li>
  <li><label for="file">Open the backup:</label> <input type="file" id="file" accept=".lm8b,.bin,.bkp,application/octet-stream"></li>
  <li><label for="proj">Pick a project:</label> <select id="proj" disabled></select></li>
  <li><label for="what">Pick its song or a scene:</label> <select id="what" disabled></select>
      <label for="loops">Times:</label> <select id="loops" disabled><option>1</option><option>2</option>
      <option>4</option><option>8</option><option>16</option></select></li>
  <li><button id="go" disabled>Export</button> <progress id="bar" max="1" value="0" hidden></progress></li>
</ol>
<p id="alert" role="status" aria-live="polite"></p>
<p>Every voice sounds in the file, so a busy song can sound fuller than it does on the FM-1.</p>
```

  The script: fetch `../emu/app.wasm` once; on a file, post `{type: "setup", wasm, file}` (copies of the
  ArrayBuffers) to `new Worker("worker.js", {type: "module"})`; fill `proj` from `slots` ("Proj 1 · 120 bpm ·
  2 scenes", an empty one disabled); on a project, post `open` and fill `what` ("Song" first if `song > 0`, then
  "Scene N" for each bit of `scenes`); `loops` enabled only for a scene; Export posts `render` and moves the
  progress bar; on `wav`, save it with a Blob and an `<a download>` click, named `loom-8-proj-N-song.wav` or
  `loom-8-proj-N-scene-S.wav`; on `error`, put the message in `#alert`. Check the backup page's real file extension
  (base/web/backup's `fileName` and LOOM-8's backup page) and use it in `accept`.

- [ ] **Step 3: make_pages.** Build `export/` like `samples/`: dress the page, copy `render.js` and `worker.js`,
  and make the samples' code reachable from the worker (Task 3 Step 4's choice). Add `("export/", "Export")` to
  `SECTIONS` and a card on the index: `<a href="export/"><strong>Export</strong><span>Make a WAV of a song or a
  scene from one of your projects, in your browser.</span></a>`. Link it from the backup page's note and the
  samples page if they have a place for "what next".

- [ ] **Step 4: Try it in a browser.** Build the site into a temp folder, serve it (`python3 -m http.server -d
  /tmp/loom-site 8765`), open `http://localhost:8765/export/` in Chrome (claude-in-chrome), open
  `app/build/scenarios/export_fixture/export.bkp`, pick Proj 1 / Song, Export; confirm the download's name and that
  the alert shows nothing wrong. Don't play the file aloud.

- [ ] **Step 5: Accessibility** — run fm1-reader's `tools/a11y/check.mjs` on the built page (see
  `skill/fm1-screen-reader/PAGES.md`) and fix what it reports.

- [ ] **Step 6: Manual.** Add, near the backup section:

```markdown
## Export a song

Use the export page to make a WAV file of a project's song, or of one scene played a few times. Save a backup first
(on the backup page, or from the emulator), open it on [the export page](../export/), pick the project and what to
play, and press **Export**. It renders in your browser, faster than real time. Every voice sounds in the file, so
a busy song can sound fuller than it does on the FM-1.
```

- [ ] **Step 7: Run everything** — `sh tests/run_tests.sh` → no `FAIL`.

- [ ] **Step 8: Commit**

```bash
git add app/web/export/index.html tools/make_pages.py docs/MANUAL.md tests/
git commit -m "LOOM-8: the export page: a song or a scene from a backup, as a WAV, in the browser (an idea from gfcalvi's Bloomy)"
```
