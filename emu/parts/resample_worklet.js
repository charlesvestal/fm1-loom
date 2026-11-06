// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "resample" (on in every build whose page takes parts; fm1.toml emu = ["-resample"] leaves it out,
// tools/build.py EMU_DEFAULT): the FM-1 makes 44.1 kHz; the page asks the browser for an
// AudioContext at 44,100 Hz, and a browser that runs it at another rate (48 kHz, 96 kHz, 22.05 kHz...) would play
// the device's samples one for one: off pitch and off tempo. With this part the worklet runs the device at its own
// rate and resamples (a 4-point cubic Hermite curve through the device's samples) to the context's, so the device's
// clock keeps time with the page's. At 44.1 kHz it does nothing.

const DEVICE_RATE = 44100;
const KEEP = 8192;                       // device frames held: a quantum's worth at any rate down to 3 kHz

function hermite(a, b, c, d, f) {        // the curve through b (f = 0) and c (f = 1), its slopes from a and d
  const c1 = 0.5 * (c - a), c2 = a - 2.5 * b + 2 * c - 0.5 * d, c3 = 0.5 * (d - a) + 1.5 * (b - c);
  return ((c3 * f + c2) * f + c1) * f + b;
}

fm1Part({
  name: "resample",
  load(fm1) {
    if (sampleRate === DEVICE_RATE) return;
    const ex = fm1.ex, step = DEVICE_RATE / sampleRate;
    const src = [new Float32Array(KEEP), new Float32Array(KEEP)];
    let len = 1, at = 1;                 // frames held (a silent one before the first); the read position among them
    const render = (n) => {
      const need = Math.floor(at + n * step) + 3;
      while (len < need) {
        const k = Math.min(1024, need - len);
        ex.web_render(k);
        src[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), k), len);
        src[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), k), len);
        len += k;
      }
      const outs = [new Float32Array(ex.memory.buffer, ex.web_out_l(), n), new Float32Array(ex.memory.buffer, ex.web_out_r(), n)];
      for (let i = 0; i < n; i++, at += step) {
        const j = Math.floor(at), f = at - j;
        for (let ch = 0; ch < 2; ch++) {
          const s = src[ch];
          outs[ch][i] = hermite(s[j - 1], s[j], s[j + 1], s[j + 2], f);
        }
      }
      const drop = Math.floor(at) - 1;   // keep one frame before the read position
      for (const s of src) s.copyWithin(0, drop, len);
      len -= drop;
      at -= drop;
    };
    fm1.ex = { ...ex, web_render: (n) => render(Math.min(n >>> 0, 1024)) };
  },
});
