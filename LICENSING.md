# LOOM-8 licensing

LOOM-8 is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full
text in `LICENSE`). If you distribute LOOM-8, or firmware derived from it, you must give your recipients its
complete corresponding source under the same licence.

It is built on [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), in `base/` (a git submodule; felucca-bsp is `base/bsp`).
Their files are **MPL-2.0, file by file**: they come from [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (@kurogedelic, Hügelton Instruments), whose code is in them under MPL-2.0 with his permission
(hugelton/felucca-bsp#4), and from Charles Vestal's platform layer of the FM-1 firmwares; see `base/LICENSING.md`
and `base/CREDITS.md`. MPL-2.0 files may be part of a GPL work, and stay MPL-2.0: changes to them are published
under MPL-2.0. LOOM-8 keeps its own licence, GPL-3.0-only; moving onto fm1-base did not change it.

LOOM-8 is inspired by the teenage engineering OP-XY's way of working. It is not affiliated with,
endorsed by or derived from teenage engineering; the name is only used descriptively.

## Where the code comes from

LOOM-8 began on its own copy of Felucca's platform layer (through X0X and FoMni). On fm1-base that copy is gone:

| Kind | Files | What happened to them |
| --- | --- | --- |
| The platform (Felucca's, as changed for X0X, FoMni, NuSaw and LOOM-8) | `firmware/hal/`, `firmware/loader/`, `firmware/crt0.S`, `firmware/app.ld`; `firmware/src/{main,usb,lcd,libc,storage,ota,midi_uart,announce}.c` and `gfx.c`'s platform half; `firmware/src/app/{main_fm1,plat_fm1,panel}.c` and `plat.h`; `tools/{build,fm1_install,fm1_rescue,fm1pkg_make,lz4blk,fm1_debug,gen_words,fit_fastmath}.py`, `tools/{build,get_toolchain,fm1_rescue}.sh`; the web installer (`web/xy_installer.html`, `web/fm1*.js`, `web/make_site.py`), `web/emu/xy_web.c`, `host/build_host.sh`, `assets/fonts/`; their tests (`tests/{ldr_test,ota_test,storage_test}.c`, `tests/install_test.py`, `tests/host/{encoder,uac,trs,fastmath}_test.c`) | dropped: fm1-base and felucca-bsp provide them (`base/device/`, `base/include/plat.h`, `base/bsp/`, `base/tools/`, `base/web/`, `base/assets/fonts/`, `base/tests/`) |
| LOOM-8's own code | the DSP (`app/dsp/`), the sequencer (`app/seq/`), the engine (`app/engine.c`), the project (`app/project.c`, `app/lz.c`), the pages and the input (`app/pages.c`, `app/ui.c`), the user sample bank over USB (`app/samples.c`), the glue to fm1-base (`app/app.h`, `app/app.c`), the simulator's part (`app/host.c`), its emulator page (`app/web/emu/`), the words (`app/words.tsv`, `app/web/words/`), the tools (`tools/`), the tests (`tests/`), the docs (`docs/`) | moved into `app/` (and `tools/`, `tests/`), GPL-3.0-only |
| LOOM-8's platform additions, kept in the app | its drawing over fm1-base's canvas (`app/gfx_loom.c`: its compact fonts, `tools/gen_font.py`; anti-aliased lines and dots; the layout audit); its second core (`app/cpu1/`, X0X's, and the extras in `app/plat_loom.c`: the profiler's clock, the supplies, the copy before the current one of a store object); its size pass (`tools/size_fns.py`) | in the app, on fm1-base's hooks (fm1.toml `asm`, `vendored`, `ir_pass`, `gen`; app.h `APP_SYSEX`, `APP_SPLASH`, `APP_CSTART`, `APP_FAULT`; `.c1_text`) |
| Third-party | below | under their own licences |

Where LOOM-8's own parts come from:

| What | Origin | Licence |
| --- | --- | --- |
| `app/cpu1/` (the second core), `app/plat_loom.c`, `app/host.c`, `app/web/emu/` | adapted from X0X and NuSaw (themselves from Felucca's `main.c`, `audio.c`), with what Melodee's dual-core audio (keremimo/melodee, GPL-3.0) and Jangada (zednaked/jangada) found on the device | GPL-3.0-only |
| `app/gfx_loom.c`, `tools/gen_font.py` | Felucca's `gfx.c` and `gen_font.py`, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for X0X and LOOM-8 | GPL-3.0-only |
| `app/samples.c` (the bank's SysEx commands) | adapted from X0X's `editor.c` (itself Felucca's editor protocol, Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments) | GPL-3.0-only |
| The engine's command queue, the overload guard, the two-core render split, `app/lz.c` and the project's packed stream, the sequencer's discipline | X0X | GPL-3.0-only |
| `tools/size_fns.py` (built for size) | the idea is Melodee's (`tools/size_fns.py` there) | GPL-3.0-only |
| The menu paradigm (a button per set of knobs, SELECT turns the pages) | [Polly](https://github.com/charlesvestal/fm1-polly) | GPL-3.0-only |
| The screen reader's words (`app/words.tsv`, `app/web/words/`) | [fm1-reader](https://github.com/charlesvestal/fm1-reader)'s protocol (fm1-base's `device/announce.c`, `tools/gen_words.py`) | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware (`app/web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| brocade, the supersaw engine in `app/dsp/synth.c` (its saw bank: the detune offsets and curve, the mix and its normalisation, the pans, the keytracked high-pass) | after [NuSaw](https://github.com/charlesvestal/nusaw-plugin) (the plugin's `dsp/nusaw_engine.cpp`, through its FM-1 port fm1-nusaw), Copyright (c) 2026 Charles Vestal | MIT (below) |
| `app/dsp/braids.h` (the engines cane, voile and gingham: their models) | ported from [Mutable Instruments Braids](https://github.com/pichenettes/eurorack/tree/master/braids) (`digital_oscillator.cc`, `macro_oscillator.cc`, the tables of `resources/`), Copyright 2012 Emilie Gillet | MIT (below) |
| `app/dsp/psxrev.h` (the reverb's PlayStation modes) | ported from [schwung-psxverb](https://github.com/charlesvestal/move-everything-psxverb), Copyright (c) 2025 Charles Vestal; the register sets and the resampling filter are psx-spx's documentation of the PlayStation's SPU | MIT (below) |
| Everything else in `app/`, `tools/`, `tests/` | LOOM-8 | GPL-3.0-only |

## Third-party material

- The fonts LOOM-8 draws with (`base/assets/fonts/`, rendered by `tools/gen_font.py`): Barlow Semi Condensed
  (SIL Open Font License 1.1, `base/assets/fonts/Barlow-OFL.txt`) and Terminus (SIL Open Font License 1.1,
  `base/assets/fonts/Terminus-LICENSE.txt`).
- The package tool (fm1-base's `tools/fm1pkg_make.py`) uses three files of JieLi's AC79 SDK (Apache-2.0), which
  are not part of this tree: see `BUILDING.md`. LOOM-8 links nothing of JieLi's (no Bluetooth, no WiFi).
- The four factory breaks (`app/samples/classic.wav`, `heat.wav`, `choke.wav`, `ecstasy.wav`: a bar each, as labelled, of
  "The Classic", "Heat", "Choke" and "Ecstasy", cut and resampled) are free loops by u/FCTRYBEATS, shared on Reddit,
  who wrote: "You have my full permission to use these loops in commercial or personal projects."
  (https://www.reddit.com/r/Drumkits/comments/ovkpgs/comment/h7db0wl/)
- The factory drum samples (`app/samples/909_kick.wav`, `909_snare.wav`, `909_hat.wav`, `909_clap.wav`) are renders
  of [9W9](https://github.com/athousanddetails/schwung-9W9), a TR-909 model (its ER-99 engine and its hat samples),
  GPL-3.0, cut by `tools/prep_909.py`.
- The recorded instruments (`app/samples/piano.wav`, `upright.wav`, `strings.wav`, `brass.wav`, `flute.wav`,
  `orch_hit.wav`) are cut from the Versilian Studios Chamber Orchestra 2 Community Edition (VSCO 2 CE,
  github.com/sgossner/VSCO-2-CE), CC0 1.0, by `tools/prep_recordings.py`.
- The reverb's PlayStation modes come from schwung-psxverb, and
  brocade's saw bank from NuSaw (Copyright (c) 2026 Charles Vestal), under the MIT License:

```
Copyright (c) 2025 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- The Braids engines (`app/dsp/braids.h`) are ported from Mutable Instruments Braids, under the MIT License:

```
Copyright 2012 Emilie Gillet.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
