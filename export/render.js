// SPDX-License-Identifier: MPL-2.0
// LOOM-8's WAV export (the export page's worker, and tests/host/export_web.mjs): the emulator's app.wasm, a backup
// file restored into it (the projects, then the samples through LOOM-8's own SysEx, as the backup page sends them),
// a project's song or a scene played to its end faster than real time, written as a 16-bit WAV. The wasm's clock
// never moves, so its overload guard sees 0 %: every voice sounds, and a project always gives the same file.

const BACKUP_MAGIC = 0x42384D4C;                   // "LM8B" (app.h APP_BACKUP_MAGIC)
const BLOCK = 1024, RATE = 44100, TAIL_MAX = 20, QUIET = 1e-4, QUIET_S = 1;   // -80 dBFS for one second

let ex = null, mem = null, extrasFn = null;
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const CRC_T = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
function crc32(b) {                                // zlib's, as base's backup page checks the file
  let c = 0xFFFFFFFF;
  for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

/** fn: a function that returns the backup page's EXTRAS (app/web/backup/extras.js), for the samples */
export function useExtras(fn) { extrasFn = fn; }

// a file -> {image: the backup itself, sections: [{tag, data}]}, laid out as base/web/backup/index.html's split
// reads it; throws in plain words. The objects' CRCs are the wasm's to check (loom_x_restore).
function split(file) {
  const bad = () => new Error("This backup is damaged.");
  if (file.length < 4 || u32(file, 0) !== BACKUP_MAGIC) throw new Error("That file is not a LOOM-8 backup.");
  if (file.length < 20 || u32(file, 12) !== crc32(file.subarray(0, 12))) throw bad();
  let pos = 16;
  for (let k = 0, n = file[6]; k < n; k++) {
    if (pos + 8 > file.length) throw bad();
    pos += 8 + (file[pos + 2] | file[pos + 3] << 8);
  }
  if (pos + 4 > file.length) throw bad();
  const image = file.subarray(0, pos + 4), sections = [];
  for (let at = pos + 4; at < file.length;) {
    if (at + 8 > file.length) throw bad();
    const tag = String.fromCharCode(...file.subarray(at, at + 4)), ln = u32(file, at + 4);
    if (at + 8 + ln + 4 > file.length) throw bad();
    const data = file.subarray(at + 8, at + 8 + ln);
    if (u32(file, at + 8 + ln) !== crc32(data)) throw bad();
    sections.push({ tag, data });
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

// a link to the wasm for extras.js, as base's backup page gives it a USB one (send, read(ms), lost): a frame goes in
// through loom_x_sx; the app's answers (several frames at once, perhaps) come out of loom_x_out as it runs
function wasmLink() {
  const queue = [], room = 1024;                   // web.c's x_sx
  const take = () => {
    const n = ex.loom_x_out_len();
    if (!n) return;
    const out = new Uint8Array(mem.buffer, ex.loom_x_out(), n).slice();
    ex.loom_x_out_clear();
    for (let i = out.indexOf(0xF0); i >= 0;) {
      const j = out.indexOf(0xF7, i);
      if (j < 0) break;
      queue.push(out.slice(i, j + 1));
      i = out.indexOf(0xF0, j + 1);
    }
  };
  return {
    lost: false,
    send(bytes) {
      if (bytes.length > room) throw new Error("A piece of the samples is too big to send.");
      new Uint8Array(mem.buffer, ex.loom_x_sx_buf(), bytes.length).set(bytes);
      ex.loom_x_sx(bytes.length);
      ex.web_render(128);
      take();
      return true;
    },
    async read(ms) {                               // (the app runs as the wasm renders: up to ~1 s of its time)
      for (let k = 0; !queue.length && k < 50; k++) { ex.web_render(BLOCK); take(); }
      return queue.shift() || null;
    },
  };
}

/** the backup in file (bytes) set up in a fresh app.wasm: the three slots, {slot, tempo (x10), scenes} or null */
export async function exportSetup(wasm, file) {
  const { image, sections } = split(file);
  await boot(wasm);
  if (image.length > ex.loom_x_buf_max()) throw new Error("This backup is damaged.");
  new Uint8Array(mem.buffer, ex.loom_x_buf(), image.length).set(image);
  const rc = ex.loom_x_restore(image.length);
  if (rc === 3) throw new Error("That file is not a backup of this LOOM-8. Update this page, or the FM-1.");
  if (rc) throw new Error("This backup is damaged.");
  const smp = sections.find((s) => s.tag === "LSMP");
  if (smp) {
    const lib = extrasFn && extrasFn().find((e) => e.tag === "LSMP");
    if (!lib) throw new Error("This page can't read the samples in this backup.");
    await lib.restore(wasmLink(), smp.data);
  }
  return [0, 1, 2].map((slot) => {
    const v = ex.loom_x_slot(slot) >>> 0;
    return v ? { slot, tempo: v & 0xFFFF, scenes: v >>> 16 & 0xFF } : null;
  });
}

/** after exportSetup: the samples in the wasm's bank (samples.c INFO 15, as extras.js reads it) */
export async function exportSamples() {
  const link = wasmLink();
  link.send([0xF0, 0x7D, 0x46, 0x4C, 15, 0, 0xF7]);
  for (let p; (p = await link.read(0));) if (p[4] === 15) return p[10];
  throw new Error("LOOM-8 stopped answering.");
}

/** after exportSetup: a slot's song slots (-1: empty) and scenes (bit s: scene s + 1) */
export function exportOpen(slot) { const song = ex.loom_x_open(slot); return { song, scenes: ex.loom_x_scenes() >>> 0 }; }

/** pick {slot, scene (-1: the song), loops}: the WAV's bytes; progress(0..1) as it goes */
export async function exportRender(pick, progress = () => {}) {
  exportOpen(pick.slot);
  const bars = ex.loom_x_start(pick.scene, pick.loops || 1), tempo = (ex.loom_x_slot(pick.slot) & 0xFFFF) / 10;
  const expect = Math.max(1, bars * 4 * 60 / tempo * RATE);
  const chunks = [];
  let frames = 0, quiet = 0, tail = 0, ended = false;
  for (;;) {
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
    if (ended) {                                   // the tail: until one second's blocks all peak under -80 dBFS
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
