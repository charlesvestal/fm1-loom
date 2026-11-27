// SPDX-License-Identifier: MPL-2.0
// app/web/presets/presets.js: a preset pack read as tools/loom_presets.py writes it (tests/fixtures/presetpack.syx:
// three presets, U3..U5, formats 18, 17 and 99), their places, the first room among the slots, the names read and a
// preset sent through a Link (a stand-in answering as samples.c does); and the page as tools/make_pages.py assembles it
// (fm1ports.js, loom_samples.js, presets.js inlined at LIB) parses.
//   node tests/host/presets_web.mjs [PYTHON]
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as P from "../../app/web/presets/presets.js";

const PY = process.argv[2] || process.env.PYTHON || "python3";
let bad = 0;
const check = (what, ok) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) bad++; };

const pack = P.parsePack(new Uint8Array(readFileSync("tests/fixtures/presetpack.syx")));
check("the pack: three presets, U3..U5, formats 18, 17, 99, named", pack.length === 3 &&
      pack.map((p) => p.slot).join() === "2,3,4" && pack.map((p) => p.fmt).join() === "18,17,99" &&
      pack[0].name === "test pad" && pack[1].name === "old delay");
check("placed from U63: two, then none past U64", P.placeFrom(pack, 62).join() === "62,63,-1");
const names = Array(64).fill("");
names[0] = "a"; names[3] = "b";
check("the first room for three: U5 (U1 and U4 taken)", P.firstRoom(names, 3) === 4);
check("no room for 65", P.firstRoom(names, 65) === -1);

// a stand-in Link: USER_NAMES 16 a reply, USER_PUT answered rc 0 (format 99: 9), as samples.c
const sent = [];
const link = {
  async retry(cmd, args) { return this.req(cmd, args); },
  async req(cmd, args) {
    if (cmd === P.USER_NAMES) {
      const from = args[0], k = Math.min(16, 64 - from), r = [from, k, 64];
      for (let i = 0; i < k; i++) r.push(...[...names[from + i]].map((c) => c.charCodeAt(0)), 0);
      return r;
    }
    if (cmd === P.USER_PUT) { sent.push(args); return [args[0], (args[1] | args[2] << 7) === 99 ? 9 : 0]; }
    throw new Error("no such command");
  },
};
const got = await P.readNames(link);
check("the names read: 64, U1 and U4 as set", got.length === 64 && got[0] === "a" && got[3] === "b" && got[1] === "");
const rc = [];
for (let i = 0; i < pack.length; i++) rc.push(await P.putPreset(link, 10 + i, pack[i]));
check("sent: U11..U13, their data as packed; the newer format refused (9)",
      rc.join() === "0,0,9" && sent.map((a) => a[0]).join() === "10,11,12" &&
      sent[0].slice(3).join() === pack[0].data.join());

// a backup (presets format 19: tests/fixtures/presets_v19.bkp, a preset and a kit) read as loom_presets.py reads it:
// the same messages, byte for byte, as its frombackup made (tests/fixtures/kitpack.syx)
const items = P.parseBackup(new Uint8Array(readFileSync("tests/fixtures/presets_v19.bkp")));
const kp = P.parsePack(new Uint8Array(readFileSync("tests/fixtures/kitpack.syx")));
check("a backup: a preset and a kit (round sub, Nylon), as loom_presets.py frombackup has them",
      items.length === 2 && items[0].kind === P.USER_PUT && items[1].kind === P.KIT_PUT && items[1].name === "Nylon" &&
      JSON.stringify(items.map((x) => [x.kind, x.fmt, x.data])) === JSON.stringify(kp.map((x) => [x.kind, x.fmt, x.data])));
check("a backup's message as frame() makes it is the pack's", JSON.stringify(P.frame(P.KIT_PUT, 0, items[1].fmt,
      P.packUnpack(items[1].data))) === JSON.stringify([0xF0, ...readFileSync("tests/fixtures/kitpack.syx").subarray(
      readFileSync("tests/fixtures/kitpack.syx").indexOf(0xF0, 1) + 1)].slice(0, items[1].data.length + 9)));

// the page as assembled
const page = execFileSync(PY, ["-c", `
import sys; sys.path.insert(0, "tools"); sys.path.insert(0, "base/tools")
import make_site as base_site
from make_site import strip_module
from pathlib import Path
src = Path("app/web/presets/index.html").read_text()
lib = base_site.ports_js() + "\\n" + strip_module(Path("app/web/samples/loom_samples.js").read_text()) + "\\n" + strip_module(Path("app/web/presets/presets.js").read_text())
print(src.replace("/*LIB*/", lib))
`], { encoding: "utf8", maxBuffer: 1 << 26 });
const m = page.match(/<script type="module">([\s\S]*?)<\/script>/);
const dir = mkdtempSync(join(tmpdir(), "loom-presets-"));
writeFileSync(join(dir, "page.mjs"), m ? m[1] : "");
let parses = !!m;
try { execFileSync(process.execPath, ["--check", join(dir, "page.mjs")], { stdio: "pipe" }); } catch (e) { parses = false; console.log(String(e.stderr)); }
check("the page as assembled parses (fm1ports.js, loom_samples.js, presets.js at LIB)", parses);
console.log(bad ? "PRESETS WEB TESTS FAILED" : "presets web tests passed");
process.exit(bad ? 1 : 0);
