// SPDX-License-Identifier: GPL-3.0-only
// app/web/samples/loom_samples.js against tools/loom_samples.py (and sampleio.py): the samples page builds the bank
// the command line builds.
//   1. the same int16 samples -> the same bank image, byte for byte (layout, the update record's marks, header, CRC)
//   2. WAV files -> the same samples at a rate (numpy's path: within a rounding), the same loop points
//   3. loom_samples.py build (auto rate) -> the same rate chosen and the same layout (the header but its CRC)
//   4. a bank read back (keptOf) and built again -> the same image; with a sample removed, the rest kept as stored
//   node tests/host/samples_web.mjs [PYTHON]     (PYTHON: one with numpy for 2 and 3; without, they are skipped)
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as L from "../../app/web/samples/loom_samples.js";

const PY = process.argv[2] || process.env.PYTHON || "python3";
const dir = mkdtempSync(join(tmpdir(), "loom-samples-"));
let bad = 0;
const check = (what, ok) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) bad++; };
const py = (code) => execFileSync(PY, ["-c", `import sys; sys.path.insert(0, "tools")\n${code}`],
  { encoding: "utf8", maxBuffer: 1 << 28, stdio: ["ignore", "pipe", "pipe"] });
const hasNumpy = (() => { try { py("import numpy"); return true; } catch (_) { return false; } })();

let seed = 12345;
const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return (seed >>> 8) / 16777216 - 0.5; };

// 1. the same int16 samples -> the same image
{
  const specs = [];
  const lens = [30000, 5, 64, 65, 120000, 47000, 90000, 333, 22050, 41000, 70000];   // over the windows' seams
  lens.forEach((n, k) => {
    const s = new Int16Array(n);
    for (let i = 0; i < n; i++) s[i] = Math.round(20000 * Math.sin(i * (0.01 + k * 0.003)) + 4000 * rnd());
    specs.push({ name: `Test_${k}!`, s, root: 40 + k, loop: k % 3 ? [k * 10, Math.max(1, n - k)] : null, bars: k % 5,
      rate: [11025, 16000, 22050, 32000][k % 4] });
  });
  const f = join(dir, "spec.json");
  writeFileSync(f, JSON.stringify(specs.map((p) => ({ ...p, s: Array.from(p.s) }))));
  let pyImg = null, jsImg = null, pyErr = "", jsErr = "";
  try {
    pyImg = Buffer.from(py(`import json, sampleio as sio
sp = json.load(open("${f}"))
img = sio.user_bank([(p["name"], p["s"], p["root"], tuple(p["loop"]) if p["loop"] else None, p["bars"], p["rate"]) for p in sp])
sys.stdout.write(img.hex())`), "hex");
  } catch (e) { pyErr = String(e.stderr || e).split("\n").filter(Boolean).pop(); }
  try { jsImg = Buffer.from(L.userBank(specs)); } catch (e) { jsErr = e.message; }
  if (pyErr || jsErr)
    check(`too long in both, the same way (${pyErr} / ${jsErr})`, pyErr.includes("too long") && jsErr.includes("too long"));
  else check(`the same samples: the same image (${specs.length} samples, ${jsImg.length} B)`, Buffer.compare(pyImg, jsImg) === 0);
  // a smaller set that fits (into the second window): byte for byte
  const fit = specs.filter((_, k) => [0, 1, 2, 3, 5, 6, 7, 8, 9].includes(k));
  writeFileSync(f, JSON.stringify(fit.map((p) => ({ ...p, s: Array.from(p.s) }))));
  const a = Buffer.from(py(`import json, sampleio as sio
sp = json.load(open("${f}"))
img = sio.user_bank([(p["name"], p["s"], p["root"], tuple(p["loop"]) if p["loop"] else None, p["bars"], p["rate"]) for p in sp])
sys.stdout.write(img.hex())`), "hex");
  const b = Buffer.from(L.userBank(fit));
  check(`the same samples (fitting): the same image (${fit.length} samples, ${b.length} B)`, Buffer.compare(a, b) === 0);
  for (const nm of ["Kick_C2.wav", "pad F#3", "x", "!!", "a_very_long_name_indeed"])
    check(`names: ${nm}`, L.cleanName(nm) === py(`import sampleio as sio; print(sio.clean_name(${JSON.stringify(nm)}), end="")`) &&
      L.noteFromName(nm) === Number(py(`import sampleio as sio; print(sio.note_from_name(${JSON.stringify(nm)}), end="")`)));
}

function wav(path, rate, nch, bits, seconds, loop) {
  const n = Math.round(rate * seconds), bps = bits >> 3, data = Buffer.alloc(n * nch * bps);
  for (let i = 0; i < n; i++) {
    const v = 0.6 * Math.sin(i * 2 * Math.PI * 220 / rate) * Math.exp(-i / rate) + 0.05 * rnd() + 0.2 * Math.sin(i * 2 * Math.PI * 7000 / rate);
    for (let c = 0; c < nch; c++) {
      const s = Math.max(-1, Math.min(1, c ? v * 0.7 : v)), o = (i * nch + c) * bps;
      if (bits === 16) data.writeInt16LE(Math.round(s * 32767), o);
      else data.writeIntLE(Math.round(s * 8388607), o, 3);
    }
  }
  const h = Buffer.alloc(44);
  h.write("RIFF", 0); h.write("WAVE", 8); h.write("fmt ", 12);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(nch, 22); h.writeUInt32LE(rate, 24);
  h.writeUInt32LE(rate * nch * bps, 28); h.writeUInt16LE(nch * bps, 32); h.writeUInt16LE(bits, 34);
  h.write("data", 36); h.writeUInt32LE(data.length, 40);
  let smpl = Buffer.alloc(0);
  if (loop) {
    smpl = Buffer.alloc(8 + 60);
    smpl.write("smpl", 0); smpl.writeUInt32LE(60, 4); smpl.writeUInt32LE(1, 8 + 28);
    smpl.writeUInt32LE(loop[0], 8 + 44); smpl.writeUInt32LE(loop[1], 8 + 48);
  }
  const all = Buffer.concat([h, data, smpl]);
  all.writeUInt32LE(all.length - 8, 4);
  writeFileSync(path, all);
}

const files = [
  [join(dir, "pad_C3.wav"), 44100, 2, 16, 1.5, [4410, 40000]],
  [join(dir, "brk.wav"), 48000, 1, 24, 2.0, null],
  [join(dir, "hit.wav"), 22050, 1, 16, 0.4, null],
];
for (const f of files) wav(...f);

// 2. WAV -> samples at a rate
if (hasNumpy) {
  for (const [path, , , , , loop] of files)
    for (const rate of [32000, 22050, 11025]) {
      const { rate: sr, x, loop: lp } = L.readWav(readFileSync(path));
      const js = L.atRate(x, sr, lp, rate);
      const out = JSON.parse(py(`import json, loom_samples as ls
p = ls.parse("${path}")
t = ls.at_rate(p, ${rate})
print(json.dumps({"s": t[1], "loop": t[3]}))`));
      let diff = 0, worst = 0;
      for (let i = 0; i < Math.max(js.s.length, out.s.length); i++) {
        const d = Math.abs((js.s[i] ?? 99999) - (out.s[i] ?? -99999));
        if (d) diff++;
        if (d > worst) worst = d;
      }
      const sameLoop = JSON.stringify(js.loop) === JSON.stringify(out.loop) && (loop ? !!js.loop : !js.loop);
      check(`${path.split("/").pop()} at ${rate}: ${js.s.length} samples, ${diff} differ (at most ${worst}), loop ${JSON.stringify(js.loop)}`,
        js.s.length === out.s.length && worst <= 1 && diff <= js.s.length / 1000 && sameLoop);
    }
} else console.log(`  skip WAV comparisons (${PY} has no numpy: sampleio's other path)`);

// 3. the command line's bank: the same rate, the same layout
if (hasNumpy) {
  const specs = [`${files[0][0]}`, `beat=${files[1][0]}@1`, `${files[2][0]}:62%32k`];
  const out = join(dir, "py.bin");
  const log = py(`import loom_samples as ls
from pathlib import Path
Path("${out}").write_bytes(ls.build(${JSON.stringify(specs)}, None))`);
  const pyImg = readFileSync(out);
  const items = specs.map((sp) => {
    let name = null, s = sp, rate = null, bars = 0, root = null;
    if (s.includes("=")) [name, s] = s.split("=");
    if (s.includes("%")) { const r = s.split("%"); s = r[0]; rate = { "32k": 32000 }[r[1]]; }
    if (/@\d+$/.test(s)) { bars = Number(s.split("@").pop()); s = s.replace(/@\d+$/, ""); }
    if (/:\d+$/.test(s)) { root = Number(s.split(":").pop()); s = s.replace(/:\d+$/, ""); }
    const { rate: sr, x, loop } = L.readWav(readFileSync(s));
    const stem = s.split("/").pop().replace(/\.wav$/, "");
    return { name: name || stem, x, sr, loop, root: root ?? (bars ? L.LOOP_ROOT : L.noteFromName(stem)), bars, rate };
  });
  const { img, chose } = L.build(items);
  const ph = Buffer.from(pyImg.subarray(0, L.BANK_HDR)), jh = Buffer.from(img.subarray(0, L.BANK_HDR));
  ph.fill(0, 12, 16); jh.fill(0, 12, 16);
  check(`loom_samples.py build: auto chose ${chose} Hz; the same header but the CRC (${img.length} B, ${log.trim().split("\n").pop()})`,
    img.length === pyImg.length && Buffer.compare(ph, jh) === 0);
}

// 4. read back and built again
{
  const items = files.map(([path]) => {
    const { rate: sr, x, loop } = L.readWav(readFileSync(path));
    return { name: path.split("/").pop(), x, sr, loop, root: 60, bars: 0, rate: null };
  });
  const { img } = L.build(items);
  const kept = L.keptOf(img);
  const again = L.build(kept).img;
  check(`a bank read back and built again: the same image (${kept.length} samples)`, Buffer.compare(Buffer.from(img), Buffer.from(again)) === 0);
  const fewer = L.build([kept[0], kept[2]]).img, h = L.parseHeader(fewer.subarray(0, L.BANK_HDR));
  const e = h.entries[1], k2 = kept[2];
  const same = Buffer.compare(Buffer.from(fewer.subarray(e.adpOff, e.adpOff + k2.adpcm.length)), Buffer.from(k2.adpcm)) === 0;
  check("one removed: the others kept as stored", h.count === 2 && e.n === k2.n && same && L.crc32(fewer.subarray(L.BANK_HDR)) === h.crc);
  // the frames: as loom_samples.frames sends them
  const fr = [...L.frames(img)].map(([c, a]) => [c, a]);
  writeFileSync(join(dir, "web.bin"), img);
  const pyFr = JSON.parse(py(`import json, loom_samples as ls
from pathlib import Path
img = Path("${join(dir, "web.bin")}").read_bytes()
print(json.dumps([[c, a] for c, a, _, _ in ls.frames(img)]))`));
  check(`the upload's frames: as loom_samples.py sends them (${fr.length})`, JSON.stringify(fr) === JSON.stringify(pyFr));
}
console.log(bad ? "SAMPLES WEB FAILED" : "samples web: as loom_samples.py");
process.exit(bad ? 1 : 0);
