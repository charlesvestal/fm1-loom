// SPDX-License-Identifier: MPL-2.0
// The browser build in Node: boot it, go to the Engine page (EDIT) and play T1's kit on the black keys (a kit's white keys
// there write its steps), expect sound and screen frames.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
let peak = 0;
const BLACK = [1,3,5,8,10,13,15,17,20,22,25];
const B_EDIT = 4;                                   // plat.h: the Engine page, where the keys play
ex.web_render(4096); ex.web_buttons(1 << B_EDIT); ex.web_render(4096); ex.web_buttons(0); ex.web_render(4096);
for (let i = 0; i < 400; i++) {
  if (i % 20 === 0 && i < 340) ex.web_keys(1 << BLACK[(i / 20) % 11]);
  ex.web_render(128);
  const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
  for (const v of l) peak = Math.max(peak, Math.abs(v));
}
console.log("peak", peak.toFixed(3), "blits", ex.web_blits());
process.exit(peak > 0.05 && peak < 1 ? 0 : 1);
