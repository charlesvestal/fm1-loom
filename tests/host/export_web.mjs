// SPDX-License-Identifier: MPL-2.0
// The WAV export page's render (app/web/export/render.js) in Node: the fixture's backup set up, its song rendered to
// a WAV, checked against the host simulator's render of the same song (tests/scenarios/export_fixture.xy); bad files
// refused in plain words; a backup with a sample section (the fixture's, with build/gen/user_bank.bin joined as base's
// backup page joins it) restored through extras.js into the wasm's bank.
//   node tests/host/export_web.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture [BANK]
import fs from "fs";
import { exportSetup, exportRender, exportSamples, useExtras } from "../../app/web/export/render.js";
const wasm = fs.readFileSync(process.argv[2]), dir = process.argv[3], bankPath = process.argv[4] || "build/gen/user_bank.bin";
const file = new Uint8Array(fs.readFileSync(`${dir}/export.bkp`));
let fails = 0;
const check = (what, ok, got) => { console.log(`${ok ? "  ok" : "FAIL"} ${what}: ${got}`); fails += !ok; };

// the samples' code as tools/make_pages.py assembles the backup page's EXTRAS (base's strip_module, then extras.js)
const strip = (t) => t.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const extrasSrc = strip(fs.readFileSync("app/web/samples/loom_samples.js", "utf8")) + "\n" +
                  fs.readFileSync("app/web/backup/extras.js", "utf8");
useExtras(new Function(extrasSrc));

// a WAV's samples as floats, by its fmt chunk (16-bit, 24-bit or float), interleaved
function pcm(wav) {
  const v = new DataView(wav.buffer, wav.byteOffset, wav.byteLength);
  let o = 12, fmt = 1, bits = 16, data = null;
  while (o + 8 <= wav.length) {
    const id = String.fromCharCode(...wav.subarray(o, o + 4)), n = v.getUint32(o + 4, true);
    if (id === "fmt ") { fmt = v.getUint16(o + 8, true); bits = v.getUint16(o + 22, true); }
    if (id === "data") { data = [o + 8, Math.min(n, wav.length - o - 8)]; break; }
    o += 8 + n + (n & 1);
  }
  const bs = bits / 8, out = new Float32Array(Math.floor(data[1] / bs));
  for (let i = 0, p = data[0]; i < out.length; i++, p += bs)
    out[i] = fmt === 3 ? v.getFloat32(p, true)
           : bits === 24 ? ((wav[p] | wav[p + 1] << 8 | wav[p + 2] << 16) << 8 >> 8) / 8388608
           : v.getInt16(p, true) / 32768;
  return out;
}
const rms = (s, from, n) => { let a = 0; for (let i = from; i < from + n; i++) a += s[i] ** 2; return Math.sqrt(a / n); };
const first = (s) => { let i = 0; while (i < s.length && Math.abs(s[i]) < 1e-3) i++; return i & ~1; };   // (a frame's left)

// bad files
for (const [what, bytes, msg] of [["not a backup", new Uint8Array(64), "That file is not a LOOM-8 backup."],
                                  ["cut short", file.subarray(0, 40), "This backup is damaged."]]) {
  try { await exportSetup(wasm, bytes); check(what, false, "accepted"); }
  catch (e) { check(what, e.message === msg, e.message); }
}

// the samples: the fixture with a bank as its LSMP section (tag, u32 length, the bytes, u32 CRC-32)
const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc32 = (b) => { let c = 0xFFFFFFFF; for (const x of b) c = crcT[(c ^ x) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
const bank = new Uint8Array(fs.readFileSync(bankPath)), want = bank[6];   // (the bank header's count)
const joined = new Uint8Array(file.length + 12 + bank.length), jv = new DataView(joined.buffer);
joined.set(file);
joined.set([..."LSMP"].map((c) => c.charCodeAt(0)), file.length);
jv.setUint32(file.length + 4, bank.length, true);
joined.set(bank, file.length + 8);
jv.setUint32(file.length + 8 + bank.length, crc32(bank), true);
let slots = await exportSetup(wasm, joined);
check("with samples: slots", slots[0] && slots[0].tempo === 1200 && slots[0].scenes === 2, JSON.stringify(slots));
const got = await exportSamples();
check("with samples: the bank's count", got === want && want > 0, `${got} of ${want}`);
const damaged = joined.slice();
damaged[file.length + 8 + 100] ^= 1;
try { await exportSetup(wasm, damaged); check("a damaged section", false, "accepted"); }
catch (e) { check("a damaged section", e.message === "This backup is damaged.", e.message); }

// the plain fixture (no section): its song to a WAV
slots = await exportSetup(wasm, file);
check("slots", slots[0] && slots[0].tempo === 1200 && slots[0].scenes === 2 && !slots[1] && !slots[2], JSON.stringify(slots));
let last = 0;
const wav = await exportRender({ slot: 0, scene: -1 }, (p) => { last = p; });
const v = new DataView(wav.buffer, wav.byteOffset);
check("RIFF WAVE", String.fromCharCode(...wav.subarray(0, 4)) === "RIFF" && String.fromCharCode(...wav.subarray(8, 12)) === "WAVE", "");
check("16-bit 44.1 kHz stereo", v.getUint16(22, true) === 2 && v.getUint32(24, true) === 44100 && v.getUint16(34, true) === 16, "");
const s = pcm(wav), secs = s.length / 2 / 44100;
check("length: the song, then its tail", secs >= 8 && secs <= 8 + 20 + 1024 / 44100, secs.toFixed(2));
const end = rms(s, s.length - 88200, 88200);
check("ends quiet (the last second under -80 dBFS)", end < 1e-4, end.toExponential(2));
check("progress reached 1", last === 1, last);
const ref = pcm(new Uint8Array(fs.readFileSync(`${dir}/export_ref.wav`)));
const a = rms(s, first(s), 8 * 88200), b = rms(ref, first(ref), 8 * 88200), db = 20 * Math.log10(a / b);
check("level: the host's render within 1 dB", Math.abs(db) <= 1, `${db.toFixed(2)} dB (${a.toFixed(4)} / ${b.toFixed(4)})`);
process.exit(fails ? 1 : 0);
