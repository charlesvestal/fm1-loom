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
check("slots 2, 3 empty", !slot(1) && !slot(2), `${JSON.stringify(slot(1))} ${JSON.stringify(slot(2))}`);
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
let bars = ex.loom_x_start(-1, 0);
check("song: 4 bars", bars === 4, bars);
let r = renderToEnd(30);
check("song: 4 bars at 120 (8 s)", Math.abs(r.s - 8) <= 0.1, r.s.toFixed(3));
check("song: sound", r.peak > 0.01, r.peak.toFixed(4));
ex.loom_x_open(0);
bars = ex.loom_x_start(1, 3);
check("scene 2 three times: 3 bars", bars === 3, bars);
r = renderToEnd(30);
check("scene 2 three times: 3 bars (6 s)", Math.abs(r.s - 6) <= 0.1, r.s.toFixed(3));
check("scene 2: sound", r.peak > 0.01, r.peak.toFixed(4));
process.exit(fails ? 1 : 0);
