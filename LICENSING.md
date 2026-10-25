# LOOM-8 licensing

LOOM-8 is free software under the Mozilla Public License, version 2.0 (`MPL-2.0`, full text in `LICENSE`), file by
file: each file says its licence in its first lines (`SPDX-License-Identifier`). If you distribute LOOM-8, or
firmware made from it, the MPL-2.0 files' source (with your changes to them) must be available under MPL-2.0.

LOOM-8 was GPL-3.0-only up to 0.7. Its author relicensed it to MPL-2.0 for 0.8, so that it can link JieLi's
Bluetooth libraries (binary only), as fm1-base allows a firmware to. Before that, every part that was not his own
was checked (fm1-base's `skill/felucca-to-fm1-base/RULES.md`): fm1-base's `tools/provenance.py` finds no lines of
Felucca, Melodee or Jangada outside `base/`; the files that said they were adapted were compared function by
function; what had been taken over was written again from what it does (the sample tools' WAV reading and IMA
ADPCM, the samples protocol's helpers in `app/samples.c`, the tests' instruction counter), or is used with its
author's permission (9W9's, below).

It is built on [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), in `base/` (a git submodule; felucca-bsp is `base/bsp`),
MPL-2.0 file by file. They come from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
(@kurogedelic, Hügelton Instruments), whose code is in them under MPL-2.0 with his permission
(hugelton/felucca-bsp#4), from keremimo's dual-core audio (felucca-bsp#5) and from Charles Vestal's platform layer of
the FM-1 firmwares; see `base/LICENSING.md` and `base/CREDITS.md`.

LOOM-8 is inspired by the teenage engineering OP-XY's way of working. It is not affiliated with,
endorsed by or derived from teenage engineering; the name is only used descriptively.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: the FM-1's hardware, the loader and the update, USB, the store, the screen and its fonts, the installer, the host simulator and the browser emulator's core, Bluetooth MIDI | fm1-base and felucca-bsp (`base/`) | MPL-2.0 |
| The DSP (`app/dsp/`, but the third-party parts below), the sequencer (`app/seq/`), the engine (`app/engine.c`), the project (`app/project.c`, `app/lz.c`), the pages and the input (`app/pages.c`, `app/ui.c`), the samples protocol (`app/samples.c`), the glue to fm1-base (`app/app.h`, `app/app.c`, `app/plat_loom.c`), the simulator's part (`app/host.c`), the emulator page (`app/web/emu/`), the web pages (`app/web/`), the words (`app/words.tsv`, `app/web/words/`), the tools (`tools/`), the tests (`tests/`), the docs (`docs/`) | Charles Vestal, Copyright (C) 2026: LOOM-8's own, including what it took from his own firmwares X0X (the engine's command queue, the overload guard, `lz.c` and the project's stream, the sequencer's discipline, the master compressor, PUMP and limiter), Polly (the menus) and NuSaw | MPL-2.0 |
| `app/gfx_loom.c`, `tools/gen_font.py`: the text drawing and the faces' rendering | fm1-base's `device/gfx.c` and `tools/gen_font.py` (Leo Kuroshita's code there, felucca-bsp#4), with LOOM-8's own compact fonts, anti-aliased drawing and layout audit | MPL-2.0 |
| The IMA ADPCM codec (`app/dsp/sample.c`, `tools/sampleio.py`, `app/web/samples/loom_samples.js`) | the IMA standard (IMA Digital Audio Focus and Technical Working Groups, "Recommended Practices for Enhancing Digital Audio Compatibility in Multimedia Systems", 1992): its tables and algorithm | MPL-2.0 (the code) |
| The plate reverb in `app/dsp/fx.c` (`do_reverb`) and the delay's time slew | after [9W9](https://github.com/athousanddetails/schwung-9W9)'s reverb (`er99_verb_tick`), by athousanddetails, through X0X's port; LOOM-8 adds the modulated taps and the stereo | MPL-2.0, with the permission of 9W9's author, athousanddetails (below) |
| The screen reader's words (`app/words.tsv`, `app/web/words/`) | [fm1-reader](https://github.com/charlesvestal/fm1-reader)'s protocol (fm1-base's `device/announce.c`, `tools/gen_words.py`) | MPL-2.0 |
| The idea of a browser emulator for FM-1 firmware (`app/web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| The idea of building the code that never runs per sample for size (`tools/size_fns.py`) | Melodee's (`tools/size_fns.py` there); no code from it | credit only |
| What the second core and the tests' timing learned on the device | Melodee's dual-core audio (keremimo) and Jangada (zednaked); no code from them in LOOM-8 (fm1-base's second core is keremimo's, by his agreement: felucca-bsp#5) | credit only |
| knit's unison in `app/dsp/synth.c` (the detune offsets and curve, the pans, the keytracked high-pass) | after [NuSaw](https://github.com/charlesvestal/nusaw-plugin) (the plugin's `dsp/nusaw_engine.cpp`, through its FM-1 port fm1-nusaw), Copyright (c) 2026 Charles Vestal | MIT (below) |
| `app/dsp/braids.h` (the engines cane and voile: their models) | ported from [Mutable Instruments Braids](https://github.com/pichenettes/eurorack/tree/master/braids) (`digital_oscillator.cc`, `macro_oscillator.cc`, the tables of `resources/`), Copyright 2012 Emilie Gillet | MIT (below) |
| `app/dsp/psxrev.h` (the reverb's PlayStation modes) | ported from [schwung-psxverb](https://github.com/charlesvestal/move-everything-psxverb), Copyright (c) 2025 Charles Vestal; the register sets and the resampling filter are psx-spx's documentation of the PlayStation's SPU | MIT (below) |
| The x models in `app/dsp/drumline.c` (x kick, x snare, x hat, x clap, x tom, x rim, x fm, x noise: `x_start`, `x_render`) | ported from [MACHINEDRUM](https://github.com/Omodaka9375/machinedrum) (`src/drums.js`, `src/audio.js`), Copyright (c) 2026 Branislav Đalić | MIT (below) |
| sophie in `app/dsp/drumline.c` (`sophie_start`, `sophie_render`) | ported from [Sophie](https://github.com/mestela/schwung-sophie) (`src/dsp/sophie.c`), Copyright (c) 2026 Matt Estela and the digisophie contributors, through SPH-16's port | MIT (below) |
| `app/dsp/digisophie.c`, `digisophie.h`, `digisophie_sin.inc` (clang) | [digisophie](https://github.com/soejrd/digisophie) (`sophie.c`), Copyright (c) 2026 Matt Estela, unchanged | MIT (below) |

## Third-party material

- The fonts LOOM-8 draws with (`base/assets/fonts/`, rendered by `tools/gen_font.py`): Barlow Semi Condensed
  (SIL Open Font License 1.1, `base/assets/fonts/Barlow-OFL.txt`) and Terminus (SIL Open Font License 1.1,
  `base/assets/fonts/Terminus-LICENSE.txt`).
- JieLi's AC79 SDK (Apache-2.0, binary libraries and a few sources; not part of this tree: see `BUILDING.md`): the
  package tool (fm1-base's `tools/fm1pkg_make.py`) uses three of its files, and the Bluetooth build (fm1.toml
  `ble`) links its Bluetooth controller and stack (`btctrler.a`, `btstack.a` and their companions), cut to a MIDI
  peripheral (`ble_trim`). `./build.sh --no-ble` builds LOOM-8 without them.
- The four factory breaks (`app/samples/classic.wav`, `heat.wav`, `choke.wav`, `ecstasy.wav`: a bar each, as labelled, of
  "The Classic", "Heat", "Choke" and "Ecstasy", cut and resampled) are free loops by u/FCTRYBEATS, shared on Reddit,
  who wrote: "You have my full permission to use these loops in commercial or personal projects."
  (https://www.reddit.com/r/Drumkits/comments/ovkpgs/comment/h7db0wl/)
- The factory kick, snare and clap (`app/samples/909_kick.wav`, `909_snare.wav`, `909_clap.wav`) are renders of
  [9W9](https://github.com/athousanddetails/schwung-9W9), a TR-909 model by athousanddetails (its circuit models),
  cut by `tools/prep_909.py`. 9W9 is GPL-3.0; the renders, and the plate reverb above, are in LOOM-8 under MPL-2.0
  with his permission, on the condition that they are credited to athousanddetails: athousanddetails, asked whether the plate reverb's code and the samples could be used in this firmware relicensed under MPL-2.0, answered (Discord, 2026-10-25): "Yeah, as long as it's stated it's athousanddetails on it, yes".
- The factory closed hat (`app/samples/909_hat.wav`) is 9W9's hat, a sample 9W9 takes from
  [ER-99](https://github.com/matthewcieplak/er-99) by Matthew Cieplak, cut by `tools/prep_909.py`. It stays under
  **GPL-3.0** (ER-99's and 9W9's licence; not athousanddetails's to relicense). It is not part of the firmware: on
  the FM-1 it lives in the sample bank, a separate file the installer or the samples page writes beside the
  firmware (`samples/factory.bin` on the site), aggregated with it, not linked into it; the firmware image has no
  copy (its factory rows are empty there). The browser emulator builds it in; the emulator links nothing of
  JieLi's, and its MPL-2.0 parts may be combined with GPL-3.0 (MPL-2.0 section 3.3).
- The recorded instruments (`app/samples/piano.wav`, `upright.wav`, `strings.wav`, `brass.wav`, `flute.wav`,
  `orch_hit.wav`) are cut from the Versilian Studios Chamber Orchestra 2 Community Edition (VSCO 2 CE,
  github.com/sgossner/VSCO-2-CE), CC0 1.0, by `tools/prep_recordings.py`.
- The reverb's PlayStation modes come from schwung-psxverb, and
  knit's unison from NuSaw (Copyright (c) 2026 Charles Vestal), under the MIT License:

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

- The x models are ported from MACHINEDRUM, sophie from Sophie, and clang is digisophie, each under the MIT License:

```
Copyright (c) 2026 Branislav Đalić (MACHINEDRUM)
Copyright (c) 2026 Matt Estela (Sophie; digisophie)
Copyright (c) 2026 digisophie contributors (Sophie)

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
