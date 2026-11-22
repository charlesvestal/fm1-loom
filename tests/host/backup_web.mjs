// SPDX-License-Identifier: MPL-2.0
// The backup page's sample section (app/web/backup/extras.js on app/web/samples/loom_samples.js, assembled as
// tools/make_pages.py assembles it) against a simulated FM-1 speaking app/samples.c's commands (INFO 15, READ 17,
// BEGIN 11, WRITE 12, HDR 16, END 13, ERASE 14), and against the command line (tools/loom_samples.py, fm1_backup.py):
//   1. a backup reads the bank back byte for byte: the image loom_samples.py checks; an empty bank: a header alone
//   2. a restore writes it back: the FM-1's bank equals it; an empty bank's restore erases
//   3. answers lost on the way (a read, a write): asked again, the same result; a late answer is not taken
//   4. a damaged section: refused before anything is sent; an FM-1 that refuses (rc): said
//   5. the .loom8 file the page writes (fm1-base's join) is one fm1_backup.py splits and loom_samples.py restores from
//   node tests/host/backup_web.mjs [PYTHON]
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { FakeFM1 as Fake } from "./fake_fm1.mjs";

const PY = process.argv[2] || process.env.PYTHON || "python3";
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const code = strip(readFileSync("app/web/samples/loom_samples.js", "utf8")) + "\n" + readFileSync("app/web/backup/extras.js", "utf8");
const [EX] = new Function(code)();
const L = new Function(strip(readFileSync("app/web/samples/loom_samples.js", "utf8")) +
  "\nreturn { BANK_HDR, BANK_SIZE, BANK_MAGIC, REGIONS, userBank, pack7, unpack7, crc32, parseHeader };")();

class FakeFM1 extends Fake { constructor(img, opts) { super(L, img, opts); } }

let bad = 0;
const check = (what, ok) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) bad++; };
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const err = async (p) => { try { await p; return null; } catch (e) { return e; } };

// a bank of three samples, as the samples page builds it
let seed = 7;
const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return ((seed >>> 8) / 16777216 - 0.5) * 30000; };
const img = L.userBank([30000, 47000, 9000].map((n, k) => ({ name: `smp${k}`, s: Int16Array.from({ length: n }, rnd),
  root: 60, loop: null, bars: 0, rate: 22050 })));
const dir = mkdtempSync(join(tmpdir(), "loom-backup-"));
const py = (code) => execFileSync(PY, ["-c", `import sys; sys.path.insert(0, "tools"); sys.path.insert(0, "base/tools")\n${code}`],
  { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });

console.log("backup page: the samples section");
{
  const dev = new FakeFM1(img);
  const got = await EX.backup(dev);
  check("1. a backup: the bank's image byte for byte", same(got.subarray(L.BANK_HDR), img.subarray(L.BANK_HDR)) &&
        same(got.subarray(0, 1344), img.subarray(0, 1344)));
  check("   ... its summary: 3 samples", /^3 samples \(\d+ KB\)$/.test(EX.check(got)));
  writeFileSync(join(dir, "sec.bin"), got);
  check("   ... the image loom_samples.py checks", py(`import loom_samples as L\nprint(L.checked(open("${dir}/sec.bin","rb").read(),"x")[6])`).trim() === "3");
  const empty = await EX.backup(new FakeFM1(null));
  check("   an empty bank: a header alone, 0 samples", empty.length === L.BANK_HDR && EX.check(empty) === "no samples");

  const back = new FakeFM1(null);
  await EX.restore(back, got);
  check("2. a restore: the FM-1's bank equals the backup", same(back.bank.subarray(0, got.length).subarray(L.BANK_HDR), got.subarray(L.BANK_HDR)) &&
        back.count() === 3);
  const full = new FakeFM1(img);
  await EX.restore(full, empty);
  check("   an empty bank's restore: erased", full.count() === 0);

  const lossy = new FakeFM1(img, { drop: [3, 40] });
  const got2 = await EX.backup(lossy);
  check("3. a read's answer lost: asked again, the same bytes", same(got2, got));
  const lossyW = new FakeFM1(null, { drop: [5], late: [9] });
  await EX.restore(lossyW, got);
  check("   a write's answer lost, another's late: the same bank", lossyW.count() === 3 &&
        same(lossyW.bank.subarray(L.BANK_HDR, got.length), got.subarray(L.BANK_HDR)));

  const dmg = Uint8Array.from(got); dmg[L.BANK_HDR + 100] ^= 1;
  const dev4 = new FakeFM1(img);
  let e = await err(EX.restore(dev4, dmg));
  check("4. a damaged section: refused, nothing sent", e && /damaged/.test(e.message) && dev4.n === 0);
  e = null; try { EX.check(dmg); } catch (x) { e = x; }
  check("   ... and said when the file is chosen", e && /damaged/.test(e.message));
  const refusing = new FakeFM1(null, { rc: { 12: 3 } });
  e = await err(EX.restore(refusing, got));
  check("   the FM-1 refuses a write: said", e && /write/.test(e.message));

  // 5. the page's file (fm1-base's join: the backup, then "LSMP", length, the bank, its CRC-32)
  const page = readFileSync("base/web/backup/index.html", "utf8");
  const m = /\/\/ --- backup protocol: begin[^\n]*\n([\s\S]*?)\/\/ --- backup protocol: end/.exec(page);
  const PORTS_JS = readFileSync("base/web/fm1ports.js", "utf8").replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
  const proto = m[1].replace(/\{\{[A-Z_]+\}\}/g, (k) => ({ "{{NAME}}": "LOOM-8", "{{ID}}": "FM-1_10", "{{MAGIC}}": "LM8B",
    "{{BACKUP_VERSION}}": "1", "{{EXT}}": "loom8", "{{PORTS_JS}}": PORTS_JS })[k] || k);   // (fm1-base make_site's fill)
  const PB = new Function(`"use strict";\n${proto}\nreturn PB;`)();
  py(`import fm1_backup as B\nopen("${dir}/proj.bin","wb").write(B.build([(1, b"p" * 300), (2, b"")], magic=b"LM8B", version=1))`);
  const proj = Uint8Array.from(readFileSync(join(dir, "proj.bin")));
  const file = PB.join(proj, [{ tag: "LSMP", data: got }]);
  writeFileSync(join(dir, "both.loom8"), file);
  const sp = PB.split(file);
  check("5. the page's .loom8: its own split gives the backup and the samples", same(sp.image, proj) && same(sp.sections[0].data, got));
  const out = py(`import fm1_backup as B, loom_samples as L\nd=open("${dir}/both.loom8","rb").read()\nimg, s = B.split(d)\n` +
                 `print(img == open("${dir}/proj.bin","rb").read(), s[0][0], L.bank_in(d, "f")[6])`).trim();
  check(`   fm1_backup.py splits it, loom_samples.py restores from it (${out})`, out === "True LSMP 3");
}
console.log(bad ? `backup web test FAILED (${bad})` : "backup web test passed");
process.exit(bad ? 1 : 0);
