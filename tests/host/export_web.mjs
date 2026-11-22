// SPDX-License-Identifier: MPL-2.0
// The WAV export page's render (app/web/export/render.js) in Node: the fixture's backup set up, its song rendered to
// a WAV, checked against the host simulator's render of the same song (tests/scenarios/export_fixture.xy); bad files
// refused in plain words; a backup with a sample section (the fixture's, with build/gen/user_bank.bin joined as base's
// backup page joins it) restored through extras.js into the wasm's bank. "Read from the FM-1" (app/web/export/usb.js,
// as tools/make_pages.py export_lib() inlines it in the page) against a simulated FM-1 on Web MIDI holding the fixture
// and the bank: the file it reads is the joined one byte for byte, and renders; nothing but reads sent; its messages;
// the page's scripts, LIB filled in, node --check'ed.
//   node tests/host/export_web.mjs app/build/emu/app.wasm app/build/scenarios/export_fixture [BANK [PYTHON]]
import fs from "fs";
import os from "os";
import path from "path";
import { execFileSync } from "child_process";
import { FakeFM1 } from "./fake_fm1.mjs";
import { exportSetup, exportRender, exportSamples, useExtras } from "../../app/web/export/render.js";
const wasm = fs.readFileSync(process.argv[2]), dir = process.argv[3], bankPath = process.argv[4] || "build/gen/user_bank.bin";
const PY = process.argv[5] || process.env.PYTHON || "python3";
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

// "Read from the FM-1": the page's LIB as tools/make_pages.py makes it
const lib = execFileSync(PY, ["-c", "import sys; sys.path.insert(0, 'tools'); import make_pages; sys.stdout.write(make_pages.export_lib())"],
  { encoding: "utf8", maxBuffer: 1 << 26 });
const USB = new Function(`${lib}\nreturn USB;`)();
const L = new Function(strip(fs.readFileSync("app/web/samples/loom_samples.js", "utf8")) +
  "\nreturn { BANK_HDR, BANK_SIZE, parseHeader, pack7, unpack7, crc32 };")();
// a simulated FM-1 on Web MIDI: the screen reader's HELLO answered with fw, LOOM-8's PING (25), the backup (BU_REQ:
// image in frames of 512 bytes, then BU_END with flags; no answer when backup is false), the bank's commands
// (tests/host/fake_fm1.mjs, as the backup page's test has them; INFO's version info)
function fm1(fw, { image = file, flags = 0, backup = true, info = 2 } = {}) {
  const dev = new FakeFM1(L, bank), sent = [];
  const ears = new Set();
  const input = { name: "LOOM-8 FM-1", state: "connected", onmidimessage: null, open: async () => {},
    addEventListener: (t, f) => ears.add(f), removeEventListener: (t, f) => ears.delete(f) };
  const reply = (d) => setTimeout(() => {
    const data = Uint8Array.from(d);
    for (const f of ears) f({ data });
    if (input.onmidimessage) input.onmidimessage({ data });
  }, 0);
  const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
  const output = { name: "LOOM-8 FM-1", state: "connected", open: async () => {}, send: (d) => {
    d = Array.from(d);
    if (d[1] === 0x7D && d[2] === 0x53 && d[3] === 0x52) {             // the screen reader's HELLO
      sent.push("hello");
      if (d[4] === 1) reply([0xF0, 0x7D, 0x53, 0x52, 1, 1, ...[...`${fw}\0https://x/words/1.json`].map((c) => c.charCodeAt(0)), 0xF7]);
      return;
    }
    if (d[1] === 0x7D && d[2] === 0x46 && d[3] === 0x4C) {             // LOOM-8's own
      sent.push(d[4]);
      if (!/^LOOM-8/.test(fw)) return;
      if (d[4] === 25) return reply([0xF0, 0x7D, 0x46, 0x4C, 25, 0, 0xF7]);
      if (d[4] === 15 && info !== 2) return reply([0xF0, 0x7D, 0x46, 0x4C, 15, info, 0, 0, 0, 0, 0, 0, 0, 0xF7]);
      const r = dev.answer(d[4], d.slice(5, -1));
      if (r) reply([0xF0, 0x7D, 0x46, 0x4C, d[4], ...r, 0xF7]);
      return;
    }
    const f = USB.PB.parseFrame(d);
    sent.push(f ? `frame ${f.cmd}` : "other");
    if (!f || f.cmd !== USB.PB.BU_REQ || !backup || !/^LOOM-8/.test(fw)) return;
    for (let o = 0; o < image.length; o += 512) reply(USB.PB.frame(USB.PB.BU_REQ, [...le32(o), ...image.subarray(o, o + 512)]));
    reply(USB.PB.frame(USB.PB.BU_END, [...le32(image.length), ...le32(USB.PB.crc32(image)), 0, flags]));
  } };
  return { midi: { inputs: new Map([[0, input]]), outputs: new Map([[0, output]]) }, sent };
}
{
  const { midi, sent } = fm1("LOOM-8 0.18");
  const said = []; let part = 0;
  const got = await USB.readFM1(midi, { status: (t) => said.push(t), progress: (f) => { if (f != null) part = f; } });
  check("read from the FM-1: the backup and its samples, as the backup page saves them", got.file.length === joined.length &&
        got.file.every((v, i) => v === joined[i]) && got.flags === 0, `${got.file.length} B`);
  check("   ... the samples' progress reached 1", part === 1, part);
  check("   ... its status says the samples take a while", said.some((t) => /samples.*up to a minute/.test(t)), said.join(" | "));
  const reads = new Set(["hello", 25, `frame ${USB.PB.BU_REQ}`, 15, 17]);
  check("   ... nothing written: only HELLO, PING, the backup's request, INFO and READ sent", sent.every((c) => reads.has(c)),
        [...new Set(sent)].join(" "));
  slots = await exportSetup(wasm, got.file);
  check("   ... set up: the fixture's slots", slots[0] && slots[0].tempo === 1200 && slots[0].scenes === 2, JSON.stringify(slots));
  const n = await exportSamples();
  check("   ... and its samples", n === want, `${n} of ${want}`);
  const w2 = await exportRender({ slot: 0, scene: 0, loops: 1 }, () => {});
  check("   ... and renders a scene", String.fromCharCode(...w2.subarray(0, 4)) === "RIFF" && w2.length > 44 + 4 * 44100, `${w2.length} B`);
  const p = await USB.readFM1(fm1("LOOM-8 0.18", { flags: 1 }).midi);
  check("   taken while playing: its flag (the page says to stop LOOM-8 for unsaved changes)", p.flags === 1, p.flags);
}
for (const [what, fw, opts, re] of [
  ["another firmware", "X0X 1.2", {}, /^The FM-1 is running X0X 1\.2, not LOOM-8\. Install LOOM-8 on the install page/],
  ["a LOOM-8 without backup", "LOOM-8 0.4", { backup: false }, /^LOOM-8 didn't answer\. If it's an older LOOM-8 without backup, update it on the Install page\./],
  ["a LOOM-8 too old for its samples", "LOOM-8 0.4", { info: 1 }, /^This LOOM-8 is too old to back up its samples\. Update it/],
]) {
  let m = "(read)";
  try { await USB.readFM1(fm1(fw, opts).midi); } catch (e) { m = e.message; }
  check(`read from the FM-1: ${what}`, re.test(m), m);
}
{
  let m = "(read)";
  try { await USB.readFM1({ inputs: new Map(), outputs: new Map() }); } catch (e) { m = e.message; }
  check("read from the FM-1: none connected", /^No FM-1 running LOOM-8 found\. Connect it by USB/.test(m), m);
}
// the page's scripts, LIB filled in as make_pages.py fills it, node --check'ed (the module one as a module)
{
  const page = fs.readFileSync("app/web/export/index.html", "utf8").replace("/*LIB*/", () => lib);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "loom-export-"));
  const scripts = [...page.matchAll(/<script( type="module")?>([\s\S]*?)<\/script>/g)];
  let ok = scripts.length === 2;
  scripts.forEach(([, mod, body], i) => {
    const f = path.join(tmp, `s${i}.${mod ? "mjs" : "js"}`);
    fs.writeFileSync(f, body);
    try { execFileSync(process.execPath, ["--check", f], { stdio: "pipe" }); } catch (e) { ok = false; console.log(String(e.stderr)); }
  });
  check("the page's scripts (LIB filled in): node --check", ok, `${scripts.length} scripts`);
}
process.exit(fails ? 1 : 0);
