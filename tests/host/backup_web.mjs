// SPDX-License-Identifier: GPL-3.0-only
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

const PY = process.argv[2] || process.env.PYTHON || "python3";
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const code = strip(readFileSync("app/web/samples/loom_samples.js", "utf8")) + "\n" + readFileSync("app/web/backup/extras.js", "utf8");
const [EX] = new Function(code)();
const L = new Function(strip(readFileSync("app/web/samples/loom_samples.js", "utf8")) +
  "\nreturn { BANK_HDR, BANK_SIZE, BANK_MAGIC, REGIONS, userBank, pack7, unpack7, crc32, parseHeader };")();

let bad = 0;
const check = (what, ok) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) bad++; };
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const err = async (p) => { try { await p; return null; } catch (e) { return e; } };

// a simulated FM-1: the bank's 216 KB (virtual offsets), the commands as samples.c answers them
class FakeFM1 {
  constructor(img, { drop = [], late = [], rc = {} } = {}) {
    this.bank = new Uint8Array(L.BANK_SIZE).fill(0xFF);
    if (img) this.bank.set(img);
    this.q = []; this.n = 0; this.drop = drop; this.late = late; this.rc = rc; this.open = false; this.hdr = null;
    this.writes = 0; this.lost = false;
  }
  count() { const h = L.parseHeader(this.bank.subarray(0, L.BANK_HDR)); return h ? h.count : 0; }
  send(bytes) {
    const cmd = bytes[4], a = bytes.slice(5, -1), k = this.n++;
    const r = this.answer(cmd, a);
    if (!r) return true;
    const msg = [0xF0, 0x7D, 0x46, 0x4C, cmd, ...r, 0xF7];
    if (this.drop.includes(k)) return true;                     // the answer lost on the way
    if (this.late.includes(k)) { this.held = msg; return true; } // ... or late: it comes after the next request's
    this.q.push(msg);
    if (this.held && !this.late.includes(k)) { this.q.unshift(this.held); this.held = null; }
    return true;
  }
  answer(cmd, a) {
    const off = (i) => a[i] | a[i + 1] << 7 | a[i + 2] << 14;
    if (cmd === 15) return [2, 0, 0, 0, 0, this.count(), 0, 0];
    if (cmd === 17) {
      const o = off(1), n = a[4] | a[5] << 7;
      if (this.open) return [0, ...a.slice(1, 4), 4];
      return [0, ...a.slice(1, 4), 0, ...L.pack7(this.bank.subarray(o, o + n))];
    }
    if (cmd === 11) { this.open = true; this.hdr = new Uint8Array(L.BANK_HDR); this.bank.fill(0xFF, 0, 4096); return [0, this.rc[11] || 0]; }
    if (cmd === 12) {
      const o = off(1), d = L.unpack7(a.slice(4));
      if (!this.open) return [0, ...a.slice(1, 4), 4];
      this.bank.set(d.subarray(0, 256), o); this.writes++;
      return [0, ...a.slice(1, 4), this.rc[12] || 0];
    }
    if (cmd === 16) { this.hdr.set(L.unpack7(a.slice(2)).subarray(0, 320), a[1] * 320); return [0, a[1], 0]; }
    if (cmd === 13) {
      const h = L.parseHeader(this.hdr);
      const ok = h && L.crc32(this.bank.subarray(L.BANK_HDR, L.BANK_HDR + h.dataLen)) === h.crc;
      if (ok) this.bank.set(this.hdr.subarray(0, 1344));
      this.open = false;
      return [0, ok ? 0 : 6];
    }
    if (cmd === 14) { this.bank.fill(0xFF); return [0, 0]; }
    return null;
  }
  async read(ms) {
    if (!this.q.length) await new Promise((r) => setTimeout(r, Math.min(ms, 20)));
    return this.q.shift() || null;
  }
  drain() { this.q.length = 0; }
}

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
  const proto = m[1].replace(/\{\{[A-Z_]+\}\}/g, (k) => ({ "{{NAME}}": "LOOM-8", "{{ID}}": "FM-1_10", "{{MAGIC}}": "LM8B",
    "{{BACKUP_VERSION}}": "1", "{{EXT}}": "loom8" })[k] || k);
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
