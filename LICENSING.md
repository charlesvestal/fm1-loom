# LOOM-8 licensing

LOOM-8 is free software, file by file: each file says its licence in its first lines (`SPDX-License-Identifier`).
Most are under the Mozilla Public License, version 2.0 (`MPL-2.0`, full text in `LICENSE`); the acid engine
(`app/dsp/bass303.c`, `bass303.h`) and the 909 kit models (`app/dsp/d909.c`, `d909.h`) are under the GNU General
Public License, version 3 only (`GPL-3.0-only`, full text in `COPYING`). The firmware and the browser emulator are built with it, so each, as a whole, is under **GPL-3.0-only**
(the MPL-2.0 files may be combined with GPL-3.0 code: MPL-2.0 section 3.3, none of them is marked "Incompatible With
Secondary Licenses"), and its whole source must be available under it when you distribute it. Each MPL-2.0 file stays
MPL-2.0 on its own: take it out of LOOM-8 and it is MPL-2.0 again. A build that linked JieLi's binary libraries
(`ble_stack = "jieli"`, WiFi) could not include the acid engine: it would have to leave it out.

LOOM-8 was GPL-3.0-only up to 0.7. Its author relicensed it to MPL-2.0 for 0.8, so that it can link JieLi's
Bluetooth libraries (binary only), as fm1-base allows a firmware to. It now runs Bluetooth on fm1-base's own stack
instead, and links nothing of JieLi's. Before that, every part that was not his own
was checked (fm1-base's `skill/felucca-to-fm1-base/RULES.md`): fm1-base's `tools/provenance.py` finds no lines of
Felucca, Melodee or Jangada outside `base/`; the files that said they were adapted were compared function by
function; what had been taken over was written again from what it does (the sample tools' WAV reading and IMA
ADPCM, the samples protocol's helpers in `app/samples.c`, the tests' instruction counter), or is used with its
author's permission (9W9's, below).

It is built on [fm1-base](https://github.com/charlesvestal/fm1-base) and
[felucca-bsp](https://github.com/hugelton/felucca-bsp), in `base/` (a git submodule; felucca-bsp is `base/bsp`),
MPL-2.0 file by file. They come from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita
(@kurogedelic, Hügelton Instruments), whose code is in them under MPL-2.0 with his permission
(hugelton/felucca-bsp#4), from keremimo's dual-core audio (felucca-bsp#5), from Melodee's USB MIDI host by Adrien
Tibi and Kerem Kilic (felucca-bsp#7, `base/bsp/NOTICE`), from Henri David's Bluetooth stack (Optimist, below) and
from Charles Vestal's platform layer of the FM-1 firmwares; see `base/LICENSING.md` and `base/CREDITS.md`.

LOOM-8 is inspired by the teenage engineering OP-XY's way of working. It is not affiliated with,
endorsed by or derived from teenage engineering; the name is only used descriptively.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform: the FM-1's hardware, the loader and the update, USB, the store, the screen and its fonts, the installer, the host simulator and the browser emulator's core, the Bluetooth glue (`base/device/bt_own*.c`) | fm1-base and felucca-bsp (`base/`) | MPL-2.0 |
| Bluetooth MIDI, pairing, MIDI devices and Bluetooth audio (`base/bt/`) and the radio drivers (`base/bsp/chip/fm1_ble.h`, `fm1_ble_rf.h`, `fm1_bredr.h`), except the parts in the next three rows | [Optimist](https://github.com/w0ts/optimist)'s own Bluetooth stack, Copyright (C) 2026 Henri David (hdavid), offered under GPL-3.0-only or MPL-2.0 ([w0ts/optimist#55](https://github.com/w0ts/optimist/issues/55)) | MPL-2.0 (taken under it) |
| The SBC encoder Bluetooth audio sends (`base/bt/br/sbc/`) | Android's Bluetooth stack (AOSP), Copyright 1999-2012 Broadcom Corporation, two files changed and marked so | Apache-2.0 (`base/LICENSES/Apache-2.0.txt`) |
| The elliptic-curve keys of Bluetooth pairing (`base/bt/br/uECC*`, `types.h`, the `.inc` files) | [micro-ecc](https://github.com/kmackay/micro-ecc) by Kenneth MacKay | BSD-2-Clause (below) |
| The radio's start-up tables (`base/bsp/chip/ble_rf_tables_v15.h`) and its calibration at every start (`base/bsp/chip/fm1_rf_cal.h`) | JieLi's AC79 SDK (Copyright (c) 2024-2026 Zhuhai Jieli Technology Co., Ltd.): the tables as M-VAVE's stock V15 firmware runs them (Copyright (c) 2026 Zhuhai Shengke / M-VAVE, `fm1_open`), the calibration rewritten in C by Charles Vestal; each header states the changes (`base/bsp/NOTICE`) | Apache-2.0 (`base/LICENSES/Apache-2.0.txt`) |
| The DSP (`app/dsp/`, but the third-party parts below), the sequencer (`app/seq/`), the engine (`app/engine.c`), the project (`app/project.c`, `app/lz.c`), the pages and the input (`app/pages.c`, `app/ui.c`), the samples protocol (`app/samples.c`), the glue to fm1-base (`app/app.h`, `app/app.c`, `app/plat_loom.c`), the simulator's part (`app/host.c`), the emulator page (`app/web/emu/`), the web pages (`app/web/`), the words (`app/words.tsv`, `app/web/words/`), the tools (`tools/`), the tests (`tests/`), the docs (`docs/`) | Charles Vestal, Copyright (C) 2026: LOOM-8's own, including what it took from his own firmwares X0X (the engine's command queue, the overload guard, `lz.c` and the project's stream, the sequencer's discipline, the master compressor, PUMP and limiter), Polly (the menus) and NuSaw | MPL-2.0 |
| `app/gfx_loom.c`, `tools/gen_font.py`: the text drawing and the faces' rendering | fm1-base's `device/gfx.c` and `tools/gen_font.py` (Leo Kuroshita's code there, felucca-bsp#4), with LOOM-8's own compact fonts, anti-aliased drawing and layout audit | MPL-2.0 |
| The IMA ADPCM codec (`app/dsp/sample.c`, `tools/sampleio.py`, `app/web/samples/loom_samples.js`) | the IMA standard (IMA Digital Audio Focus and Technical Working Groups, "Recommended Practices for Enhancing Digital Audio Compatibility in Multimedia Systems", 1992): its tables and algorithm | MPL-2.0 (the code) |
| The acid engine (`app/dsp/bass303.c`, `bass303.h`: a TB-303) | X0X's 303 engine ([fm1-x0x](https://github.com/charlesvestal/fm1-x0x), `firmware/src/dsp/bass303.c`), Copyright (C) 2026 Charles Vestal, through [schwung-303](https://github.com/charlesvestal/schwung-303): ported from [Open303](https://github.com/RobinSchmidt/Open303) by Robin Schmidt (MIT), with the Devilfish ranges of [jc303](https://github.com/midilab/jc303) by midilab (GPL-3.0) and the RAT drive ported from [dm-Rat](https://github.com/davemollen/dm-Rat) by Dave Mollen (GPL-3.0); its render in passes over sub-blocks and the coefficients its voices share are [Optimist](https://github.com/w0ts/optimist)'s (`firmware/src/acid/bass303.c`, GPL-3.0-only, the same output to the bit) | GPL-3.0-only (`COPYING`); the firmware as a whole with it |
| The 909 kit models (`app/dsp/d909.c`, `d909.h`: the kick, snare, toms, rim shot and clap, and the sampler of the hat, open hat, ride and crash) | [9W9](https://github.com/athousanddetails/schwung-9W9)'s TR-909 circuit models and its sampler, by athousanddetails (itself grown out of [ER-99](https://github.com/matthewcieplak/er-99) by Matthew Cieplak), through X0X's port (fm1-x0x `firmware/src/dsp/drum909.c`, `drum909_dsp.h`); the sampler's skip past the sample's end after [Optimist](https://github.com/w0ts/optimist)'s (GPL-3.0-only) | GPL-3.0-only (`COPYING`) |
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
- JieLi's AC79 SDK (Apache-2.0; not part of this tree: see `BUILDING.md`): the package tool (fm1-base's
  `tools/fm1pkg_make.py`) uses three of its files. No JieLi library is linked: Bluetooth runs on fm1-base's own
  stack (fm1.toml `ble_stack = "own"`), and the build checks that nothing of JieLi's is in. The radio's tables and
  calibration above are JieLi's code under Apache-2.0, in felucca-bsp's source.
- micro-ecc, in the Bluetooth stack, is under the BSD 2-Clause License (also in `base/LICENSES/BSD-2-Clause-micro-ecc.txt`):

```
Copyright (c) 2014, Kenneth MacKay
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

- The four factory breaks (`app/samples/classic.wav`, `heat.wav`, `choke.wav`, `ecstasy.wav`: a bar each, as labelled, of
  "The Classic", "Heat", "Choke" and "Ecstasy", cut and resampled) are free loops by u/FCTRYBEATS, shared on Reddit,
  who wrote: "You have my full permission to use these loops in commercial or personal projects."
  (https://www.reddit.com/r/Drumkits/comments/ovkpgs/comment/h7db0wl/)
- The factory kick, snare and clap (`app/samples/909_kick.wav`, `909_snare.wav`, `909_clap.wav`) are renders of
  [9W9](https://github.com/athousanddetails/schwung-9W9), a TR-909 model by athousanddetails (its circuit models),
  cut by `tools/prep_909.py`. 9W9 is GPL-3.0; the renders, and the plate reverb above, are in LOOM-8 under MPL-2.0
  with his permission, on the condition that they are credited to athousanddetails: athousanddetails, asked whether the plate reverb's code and the samples could be used in this firmware relicensed under MPL-2.0, answered (Discord, 2026-10-25): "Yeah, as long as it's stated it's athousanddetails on it, yes".
- The 909 cymbals (`app/samples/909/hh.wav`, `ride.wav`, `crash.wav`): ER-99's hat, ride and crash samples by
  Matthew Cieplak ([ER-99](https://github.com/matthewcieplak/er-99)), as 9W9 ships them, unchanged (copied from
  X0X's `assets/909/`). **GPL-3.0**. `tools/gen_909res.py` packs them (5-bit block companding) into the resource
  region (`loom-X.Y-res.bin` beside the package on the site and in the releases), which the installer writes beside
  the firmware after an update; the 909 hat, open hat, ride and crash models play them from there. The host
  simulator and the browser emulator build them in.
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

- The acid engine's core is Open303 by Robin Schmidt, under the MIT License (its notice, as Open303 gives it):

```
The Open303 source code is released under the terms of the MIT license:

The MIT License

Copyright (c) 2009 Robin Schmidt (www.rs-met.com)

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
