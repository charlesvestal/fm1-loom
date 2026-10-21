# Building LOOM-8

LOOM-8 is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (in `base/`, a git submodule, with
[felucca-bsp](https://github.com/hugelton/felucca-bsp) inside it as `base/bsp`). Clone with the submodules:

```
git clone --recursive https://github.com/charlesvestal/fm1-loom
git submodule update --init --recursive      # (an existing clone)
```

The build (fm1-base's `tools/build.py`, run by `./build.sh`, driven by `app/fm1.toml`) makes three files in
`app/build/`:

| File | What |
| --- | --- |
| `loom.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `loom.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow built with libraqm (the fonts need it; Homebrew's: `PYTHON=/opt/homebrew/bin/python3.14`)
- Docker Desktop, or podman. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source tree in a folder the
  container can share, e.g. under `/Users`. With podman, put a `docker` script that runs `exec podman "$@"` first
  on your PATH. `FM1B_JOBS` (default 4) limits parallel compiles: a podman machine drops connections when many
  containers start at once.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  base/tools/get_toolchain.sh       # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Emscripten and Node.js (optional: the browser emulator and its test).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh                    # the firmware: app/build/loom.fwsc
./build.sh --host             # the host simulator: app/build/host/loom_host
./build.sh --web              # the browser emulator: app/build/emu/ (LOOM-8's own page, app/web/emu/)
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations (`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).
`./build.sh --release 0.1` makes a release build: the package is `app/build/loom-0.1.fwsc`, its identity
FM-1_10XXYYZZ (a dev build's is FM-1_1000).

The build generates `app/build/gen/` (fm1-base's fonts for its own screens, LOOM-8's fonts, the wavetables, the
factory samples, the screen reader's words link, this build's number). It runs LOOM-8's size pass
(`tools/size_fns.py`: the code that never runs per sample built with LLVM's `minsize`), and checks: no
soft-double routine (a `double` crept in), no register access outside felucca-bsp (LOOM-8's own second core,
`app/cpu1/`, aside), nothing in RAM code calling the flash (the second core's loop aside), no 64-bit multiply
inlined beside a load, and the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the units' own tests (the synth, the drumline, the FX, the sequencer:
`tests/host/*_test.sh`); the DSP's fingerprints against a reference (`tests/host/dsp_hash.sh save` before a change
that must not move a sample, then the tests compare); the user sample bank's tool (`tests/host/bank_test.py`: its
layout over the three flash windows, the rates, the bounds) and an upload played through the app
(`tests/scenarios/upload.xy`); fm1-base's update-path tests
against `app/build/loom.fwsc`; the browser build; and the whole app in fm1-base's simulator
(`tests/scenarios/*.xy`, backup and restore among them), with its screenshots and audio in `app/build/scenarios/`
and the layout audit. Silent: the audio goes to WAV files. fm1-base's own tests: `base/tests/run_tests.sh`.

`app/build/host/loom_host SCRIPT OUTDIR` runs one script: fm1-base's commands are at the top of
`base/host/plat_host.c`, LOOM-8's own at the top of `app/host.c`. `tools/gen_screens.sh` makes the manual's
screenshots (`tools/manual_screens.xy`).

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/loom.fwsc
python3 base/tools/fm1_install.py --info     # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold OCT− and OCT+ for 5 seconds for the update mode. Two failed starts
in a row bring up a safe mode with USB on; the web installer can put M-VAVE's firmware back.
`base/tools/fm1_remote.py` drives an FM-1 over USB and reads its screen, crash record and log.

Backup and restore of the projects and the user presets: `python3 base/tools/fm1_backup.py backup FILE --magic LM8B`
and `restore FILE` (or the site's Backup page). The user sample bank is not in a backup: `tools/loom_samples.py`
loads it from the WAV files. The bank: 216 KB of flash in three windows (`app/dsp/sample.h`: 0xC1000..0xDBFFF, and
fm1-base's PLAT_XDATA1 0xE5000..0xFBFFF and PLAT_XDATA0 0x93000..0x96FFF), which nothing else writes: not the store
(app.c checks the map), not a restore, not an update (the loader writes below 0x93000 and erases a sector above it
only for an update record, which the bank never holds: `tools/sampleio.py`).

## Publishing a release

A release is built on GitHub from its tag, so anyone can check that the package came from this source:
`.github/workflows/release.yml` calls the shared FM-1 builder,
[`zvenson/fm1-build` at `v1`](https://github.com/zvenson/fm1-build/tree/v1) (as X0X and Melodee do). The builder
checks out the tagged commit, installs JieLi's Linux toolchain and the AC79 SDK at its fixed tag, runs
`./build.sh --release X.Y` (with fm1-base and felucca-bsp at the commits this one links) and `tests/run_tests.sh`
(a failing test stops the release), then signs build provenance for `loom-X.Y.fwsc` and `loom-X.Y-app.bin`. The
Firmware Hub shows such packages as built from source.

1. Write the notes first, as a draft: `gh release create vX.Y --draft --title "LOOM-8 X.Y" --notes-file NOTES.md`
   (the workflow keeps a draft's notes; without one it makes a draft from the commits).
2. Push the tag: `git tag vX.Y && git push origin vX.Y`. The workflow builds, tests, attests, checks the SHA-256 sums
   and the signatures against the builder, the tag and the commit, then publishes the draft. A tag with a suffix
   (`v0.6-beta`) is a prerelease. A published release is never rebuilt: a new build needs a new tag. A manual run
   on a branch (Actions, Run workflow) builds and attests a development package, in the run's `packages` artifact.
3. The site from that package: `gh release download vX.Y --pattern '*.fwsc' --dir app/build`, then
   `tools/publish_pages.sh X.Y`: it builds the site (landing page, browser emulator, fm1-base's web installer and
   backup page, the words files, the firmware) with `tools/make_pages.py` and pushes it to the `gh-pages` branch,
   which GitHub Pages serves at <https://charlesvestal.github.io/fm1-loom/>.

To check a downloaded package yourself (GitHub CLI, signed in):

```sh
tag=vX.Y repo=charlesvestal/fm1-loom
gh release download "$tag" --repo "$repo" --dir build/verify --pattern '*.fwsc'
git fetch origin "refs/tags/$tag:refs/tags/$tag"
gh attestation verify "build/verify/loom-${tag#v}.fwsc" --repo "$repo" \
  --signer-workflow zvenson/fm1-build/.github/workflows/build.yml \
  --source-ref "refs/tags/$tag" --source-digest "$(git rev-parse "$tag^{commit}")"
```

The same checks the `-app.bin`. `SHA256SUMS` comes with the downloads; a checksum alone proves nothing about where a
package was built.

A local release build (`./build.sh --release X.Y`, package `app/build/loom-X.Y.fwsc`; its identity FM-1_10XXYYZZ is
what the installer checks) is still how to try one on the FM-1 before tagging.
