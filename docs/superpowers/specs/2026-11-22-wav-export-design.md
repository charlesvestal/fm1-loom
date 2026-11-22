# WAV export page

Idea from gfcalvi's Bloomy (a fork of LOOM-8 0.9.3, MPL-2.0), which exports songs to audio in the browser. Written
fresh for LOOM-8; nothing taken from Bloomy's code.

## What it does

A page on LOOM-8's site, beside the samples and backup pages, that makes a WAV from a project without the FM-1.

1. Open a LOOM-8 backup file (from the FM-1 by the backup page, or from the emulator's Save backup). The page checks
   it (fm1-base's backup format: magic LM8B, version, CRCs) and lists its three projects by name; empty slots are
   shown but can't be picked.
2. Pick a project, then what to render:
   - **Song**, offered only when the project has one: from the song's first entry to the end of its last;
   - or **a scene**, any of the project's scenes, played 1, 2, 4, 8 or 16 times.
3. **Export**: a progress bar, then the browser saves `loom-8-proj-N-song.wav` or `loom-8-proj-N-scene-S.wav`
   (projects have no names; 16-bit, 44.1 kHz, stereo). The project list shows each slot's tempo and scenes, as
   GLO's list does (2026-11-22-glo-projects-design.md).

The page says nothing aloud and plays nothing; it only writes the file.

## The render

- A Web Worker loads the emulator's `app.wasm` (the same build the emulator page uses) and drives it through a few
  functions the app exports for this:
  - `render_open(backup)`: the projects and presets from the backup's objects and the sample bank from its LSMP
    section, as a restore would leave them;
  - `render_start(project, scene, loops)`: load the project (scene = -1: its song), start playback from the top;
  - `render_block(out)`: render one block into `out`, returning the frames written and whether the music has ended.
- The worker runs blocks as fast as it can and posts progress to the page.
- The end: a song ends when its last entry finishes; a scene after its loops. After that the render goes on until
  the output stays under -80 dBFS for one second (reverb and delay tails), at most 20 seconds.
- The overload guard is fed 0 % (as `tools/demos/render.sh`: `LOOM_LOAD=0`): every voice sounds, and the same
  project always gives the same file. A dense song can come out fuller than the FM-1 plays it.
- The level is what the FM-1 sends to USB audio, not raised or normalised.

If fm1-base's web build has no way for an app to export its own functions to the worklet's wasm, a small hook goes
into fm1-base (an app's extra exports in fm1.toml), kept general.

## Where it lives

- `app/web/export/` (page and worker), dressed by `tools/make_pages.py` like the other pages.
- The render functions in the app (a file of their own, host and web builds only; nothing in the firmware image).
- `tools/publish_pages.sh` publishes the page; the samples page, the backup page and the manual link to it.

## Errors

- Not a LOOM-8 backup, a different backup version, or a damaged file: say so, nothing else happens.
- A project from a newer format than this page's build: "made by a newer LOOM-8; update the page" (the page is
  published with each release, so it matches the current firmware).
- A song or scene with no notes still renders (silence plus the tail); the page doesn't second-guess it.

## Tests

- A Node test (`tests/host/export_web.mjs`, in `tests/run_tests.sh`) renders a fixture backup through the worker's
  code: checks the length (song bars at the tempo + tail), that it isn't silent, and that it matches the host
  simulator's render of the same project and song (its level within 1 dB).
- Bad files: wrong magic, wrong version, a cut-short file, each refused with its message.

## Not in this

MP3, stems, per-track export, rendering on the FM-1, a preview player.
