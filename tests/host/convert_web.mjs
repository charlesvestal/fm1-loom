// SPDX-License-Identifier: MPL-2.0
// app/web/presets/convert.js against tools/juno_import.py and tools/prophet_import.py: random banks (no instrument's
// data: random bytes in each control's range), every patch converted by both, the same parameters and values in the
// same order; and the presets' bytes buildPreset makes from them, on the simulator's tables (its webtables), as the
// simulator saves the same lines (loom_presets.py preset_from_lines), for a few of each.
//   node tests/host/convert_web.mjs [PYTHON]       (after ./build.sh --host)
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as C from "../../app/web/presets/convert.js";

const PY = process.argv[2] || process.env.PYTHON || "python3";
const dir = mkdtempSync(join(tmpdir(), "loom-convert-"));
let bad = 0;
const check = (what, ok) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) bad++; };
let seed = 12345;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };

// a Juno bank: 128 patches, sliders 0..127, the switches' bits at random
const juno = [];
for (let k = 0; k < 128; k++) {
  const s = Array.from({ length: 16 }, () => rnd(128));
  juno.push([0xF0, 0x41, 0x30, 0x00, k, ...s, rnd(128), rnd(32), 0xF7]);
}
writeFileSync(join(dir, "juno.syx"), Uint8Array.from(juno.flat()));
// a Prophet bank: 200 programs of 133 bytes (the switches 0/1, the three-way ones 0..2, the counts in their ranges)
const SW = new Set([3, 4, 5, 6, 7, 10, 11, 12, 20, 23, 24, 25, 27, 28, 29, 30, 31, 34, 35, 36, 38, 39, 41, 42, 51, 52]);
const prophet = [];
for (let k = 0; k < 200; k++) {
  const raw = Array.from({ length: 133 }, (_, i) => (SW.has(i) ? rnd(2) : i === 19 ? rnd(3) : i === 53 ? rnd(11)
    : i === 54 ? rnd(7) : i === 86 ? [0, 1, 6, 11][rnd(4)] : i >= 65 && i < 85 ? 65 + rnd(26) : rnd(128)));
  const pk = [];
  for (let g = 0; g < 133; g += 7) {
    const run = raw.slice(g, g + 7);
    pk.push(run.reduce((h, x, j) => h | (x >> 7) << j, 0), ...run.map((x) => x & 127));
  }
  prophet.push([0xF0, 0x01, 0x32, 0x02, Math.floor(k / 40), k % 40, ...pk, 0xF7]);
}
writeFileSync(join(dir, "prophet.syx"), Uint8Array.from(prophet.flat()));

const py = (code) => JSON.parse(execFileSync(PY, ["-c", `import sys, json; sys.path.insert(0, "tools")\n${code}`],
  { encoding: "utf8", maxBuffer: 1 << 26 }));
const pj = py(`import juno_import as J\nb = J.read_bank(${JSON.stringify(join(dir, "juno.syx"))})
print(json.dumps({k: J.convert(p)[0] for k, p in b.items()}))`);
const pp = py(`import prophet_import as P\nb = P.read_bank(${JSON.stringify(join(dir, "prophet.syx"))})
print(json.dumps({k: P.convert(p)[0] for k, p in b.items()}))`);
const jb = C.readJuno(readFileSync(join(dir, "juno.syx"))), pb = C.readProphet(readFileSync(join(dir, "prophet.syx")));
const same = (a, b) => JSON.stringify(a.map(([n, v]) => [n, String(v)])) === JSON.stringify(b.map(([n, v]) => [n, String(v)]));
let jd = 0, pd = 0;
for (const [k, p] of jb) if (!same(C.convertJuno(p).out, pj[k])) { if (!jd++) console.log("    juno", k, JSON.stringify(C.convertJuno(p).out), JSON.stringify(pj[k])); }
for (const [k, p] of pb) if (!same(C.convertProphet(p).out, pp[k])) { if (!pd++) console.log("    prophet", k, JSON.stringify(C.convertProphet(p).out), JSON.stringify(pp[k])); }
check(`Juno: all ${jb.size} patches as juno_import.py converts them`, jb.size === 128 && jd === 0);
check(`Prophet: all ${pb.size} programs as prophet_import.py converts them`, pb.size === 200 && pd === 0);

// the presets' bytes: the simulator's tables, and its own preset for the same lines
const host = "app/build/host/loom_host";
writeFileSync(join(dir, "w.xy"), `wait 50\nwebtables ${join(dir, "t.json")}\n`);
execFileSync(host, [join(dir, "w.xy"), dir], { stdio: "pipe", env: { ...process.env, LOOM_LOAD: "0" } });
const T = JSON.parse(readFileSync(join(dir, "t.json"), "utf8"));
const sample = [...[...jb].slice(0, 6).map(([k, p]) => [`juno ${k}`, C.convertJuno(p).out]),
                ...[...pb].slice(0, 6).map(([k, p]) => [`prophet ${k}`, C.convertProphet(p).out])];
let bd = 0;
for (const [what, out] of sample) {
  const lines = out.map(([n, v]) => `knob 2 ${n} ${v}`);
  const got = py(`import loom_presets as L\nf, d = L.preset_from_lines("x", ${JSON.stringify(lines)})\nprint(json.dumps([f, list(d)]))`);
  const mine = [...C.buildPreset(T, "x", out)];
  if (got[0] !== T.fmt || JSON.stringify(got[1]) !== JSON.stringify(mine)) {
    if (!bd++) console.log("    " + what, got[1].map((x, i) => (x !== mine[i] ? `${i}:${x}/${mine[i]}` : "")).filter(Boolean).join(" "));
  }
}
check(`the presets' bytes: ${sample.length} made in the page as the simulator saves them`, bd === 0);
// the level fit in the emulator (app.wasm, ./build.sh --web) against loom_presets.py level_fit in the simulator: the
// same volume within 2 (a chord's peak there and here, within ~0.6 dB)
const WASM = "app/build/emu/app.wasm";
let emu = null;
try { emu = await C.bootEmu(readFileSync(WASM)); } catch (e) { console.log(`    (no ${WASM}: ${e.message})`); }
if (emu) {
  let worst = 0, n = 0;
  for (const [what, out] of sample) {
    const lines = out.map(([n, v]) => `knob 2 ${n} ${v}`);
    const want = py(`import loom_presets as L\nls, pk, vol = L.level_fit(${JSON.stringify(lines)})\nprint(json.dumps([pk, vol]))`);
    const got = C.levelFit(emu, T, C.buildPreset(T, "x", out));
    worst = Math.max(worst, Math.abs(got.vol - want[1]));
    if (Math.abs(got.vol - want[1]) > 2) console.log(`    ${what}: volume ${got.vol} (chord ${got.pk.toFixed(1)} dB), the simulator's ${want[1]} (${want[0].toFixed(1)} dB)`);
    n++;
  }
  check(`the level fit: ${n} presets' volumes in the emulator as in the simulator (within 2; worst ${worst})`, worst <= 2);
}
console.log(bad ? "CONVERT WEB TESTS FAILED" : "convert web tests passed");
process.exit(bad ? 1 : 0);
