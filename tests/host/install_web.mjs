// SPDX-License-Identifier: GPL-3.0-only
// The installer's steps for LOOM-8 (app/web/install_app.js on app/web/samples/loom_samples.js, assembled as
// tools/make_pages.py assembles them for fm1-base's web/installer.html APP) against a simulated FM-1 on a Web MIDI
// shaped port, speaking app/samples.c's commands (PING 25, SINFO 15, READ 17, BEGIN 11, WRITE 12, HDR 16, END 13):
//   1. an old LOOM-8's bank (version 2) with room: nothing asked; after, the user's samples first, kept byte for byte
//      (their checkpoints every 128), then the factory set, marked; user 1 and 2 still theirs
//   2. no room as they are: asked; "Make room": the 32 kHz one put at 22 kHz, the other kept; it all fits
//   3. no room even at 11 kHz: asked, "Install without them": nothing sent
//   4. no LOOM-8 answering before (another firmware, or an old one): looked at after, from the one installed
//   5. the factory set already there: nothing asked, nothing sent; "Cancel" before: the install stops (false)
//   node tests/host/install_web.mjs [PYTHON]
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";

const PY = process.argv[2] || process.env.PYTHON || "python3";
const strip = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const LIB = strip(readFileSync("app/web/samples/loom_samples.js", "utf8"));
const APP = new Function(LIB + "\n" + readFileSync("app/web/install_app.js", "utf8") + "\nreturn { before, after };")();
const L = new Function(LIB + "\nreturn { BANK_HDR, BANK_SIZE, BLK, BLK_V2, HDR_USED, NFENT, E_FACTORY, userBank, " +
  "pack7, unpack7, crc32, parseHeader, keptOf, isFactory, sampleBytes };")();

// the factory set as the site publishes it (samples/factory.bin): fetch() answers it
const FACTORY = new Uint8Array(execFileSync(PY, ["-c", `import sys; sys.path.insert(0, "tools")
import gen_samples, sampleio
sys.stdout.buffer.write(sampleio.user_bank(gen_samples.factory_set()))`], { maxBuffer: 1 << 24 }));
globalThis.fetch = async (url) => ({ ok: /factory\.bin$/.test(url), status: 200, arrayBuffer: async () => FACTORY.buffer.slice(0) });

let bad = 0;
const check = (what, ok) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) bad++; };
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// a simulated FM-1 behind a Web MIDI port pair: the bank (virtual offsets), samples.c's answers; quiet: not LOOM-8
class Fake {
  constructor(img) {
    this.bank = new Uint8Array(L.BANK_SIZE).fill(0xFF);
    if (img) this.bank.set(img);
    this.quiet = false; this.sent = 0; this.open = false; this.hdr = null;
    const self = this;
    this.input = { name: "LOOM-8 FM-1", state: "connected", open: async () => {}, onmidimessage: null };
    this.output = { name: "LOOM-8 FM-1", open: async () => {}, send(d) { self.recv(d); } };
    this.midi = { inputs: new Map([["i", this.input]]), outputs: new Map([["o", this.output]]) };
  }
  count() { const h = L.parseHeader(this.bank.subarray(0, L.BANK_HDR)); return h ? h.count : 0; }
  image() { const h = L.parseHeader(this.bank.subarray(0, L.BANK_HDR)); return this.bank.slice(0, L.BANK_HDR + h.dataLen); }
  recv(d) {
    if (this.quiet) return;
    const cmd = d[4], a = d.slice(5, -1), r = this.answer(cmd, a);
    if (r && this.input.onmidimessage)
      setTimeout(() => this.input.onmidimessage({ data: Uint8Array.from([0xF0, 0x7D, 0x46, 0x4C, cmd, ...r, 0xF7]) }), 0);
  }
  answer(cmd, a) {
    const off = (i) => a[i] | a[i + 1] << 7 | a[i + 2] << 14;
    if (cmd === 25) return [0];
    if (cmd === 15) return [2, 0, 0, 0, 0, this.count(), 0, 0];
    if (cmd === 17) { const o = off(1), n = a[4] | a[5] << 7; return [0, ...a.slice(1, 4), 0, ...L.pack7(this.bank.subarray(o, o + n))]; }
    if (cmd === 11) { this.open = true; this.sent++; this.hdr = new Uint8Array(L.BANK_HDR); this.bank.fill(0xFF, 0, 4096); return [0, 0]; }
    if (cmd === 12) { const o = off(1); this.bank.set(L.unpack7(a.slice(4)).subarray(0, 256), o); return [0, ...a.slice(1, 4), 0]; }
    if (cmd === 16) { this.hdr.set(L.unpack7(a.slice(2)).subarray(0, 320), a[1] * 320); return [0, a[1], 0]; }
    if (cmd === 13) {
      const h = L.parseHeader(this.hdr);
      const ok = h && L.crc32(this.bank.subarray(L.BANK_HDR, L.BANK_HDR + h.dataLen)) === h.crc;
      if (ok) this.bank.set(this.hdr.subarray(0, L.HDR_USED));
      this.open = false;
      return [0, ok ? 0 : 6];
    }
    return null;
  }
}
const ui = (answer) => {
  const u = { asked: [], said: [], say: (t) => u.said.push(t), log: () => {}, progress: () => {} };
  u.ask = async (title, body, choices) => { u.asked.push({ title, body, choices: choices.map((c) => c[0]) }); return answer; };
  return u;
};

// an old LOOM-8's bank: version 2 (a checkpoint every 64), n samples of noise at these rates
let seed = 7;
const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return ((seed >>> 8) / 16777216 - 0.5) * 30000; };
function oldBank(specs) {
  const src = LIB.replace("const BLK = 128, BLK_V2 = 64;", "const BLK = 64, BLK_V2 = 64;").replace("BANK_VER = 3;", "BANK_VER = 2;");
  if (src === LIB) throw new Error("oldBank: the library's BLK / BANK_VER lines moved");
  const L2 = new Function(src + "\nreturn { userBank };")();
  return L2.userBank(specs.map(([name, n, rate]) => ({ name, s: Int16Array.from({ length: n }, rnd), root: 60, loop: null,
    bars: 0, rate })));
}
const run = async (dev, answer, before = true) => {
  const u = ui(answer);
  if (!before) dev.quiet = true;
  const st = await APP.before(dev.midi, u);
  dev.quiet = false;
  if (st !== false) await APP.after(dev.midi, u, st);
  return { u, st };
};

console.log("installer: LOOM-8's sounds into the sample bank");
{
  const img = oldBank([["kick2", 9000, 22050], ["loop", 30000, 16000]]);
  const dev = new Fake(img), was = L.keptOf(img);
  const { u } = await run(dev, "cancel");
  const h = L.parseHeader(dev.bank.subarray(0, L.BANK_HDR)), now = L.keptOf(dev.image());
  check("1. room: nothing asked, the bank sent once", u.asked.length === 0 && dev.sent === 1);
  check("   the user's two first (user 1, 2), their ADPCM byte for byte, their checkpoints every other one",
    h.ver === 3 && now[0].name === "kick2" && now[1].name === "loop" && !L.isFactory(now[0]) && !L.isFactory(now[1]) &&
    same(now[0].adpcm, was[0].adpcm) && same(now[1].adpcm, was[1].adpcm) && same(now[1].ckb, was[1].ckb));
  check(`   then the factory set's ${L.NFENT}, marked, as factory.bin has them`,
    h.count === 2 + L.NFENT && now.slice(2).every(L.isFactory) &&
    same(now[2].adpcm, L.keptOf(FACTORY)[0].adpcm) && u.said.at(-1) === "LOOM-8 is installed, with its sounds.");
}
{
  const img = oldBank([["big", 230000, 32000], ["small", 20000, 16000]]);   // 126 + 11 KB: + 86 does not fit
  const dev = new Fake(img), was = L.keptOf(img);
  const { u } = await run(dev, "room");
  const now = L.keptOf(dev.image());
  check("2. no room: asked, \"Make room\" offered, naming the sample", u.asked.length === 1 &&
    u.asked[0].choices.join() === "room,skip,cancel" && /<b>big<\/b>/.test(u.asked[0].body) && !/<b>small<\/b>/.test(u.asked[0].body));
  check("   made room: big at 22 kHz, small kept byte for byte, the factory set after them",
    now[0].name === "big" && now[0].rate === 22050 && Math.abs(now[0].n - 230000 * 22050 / 32000) < 4 &&
    now[1].name === "small" && same(now[1].adpcm, was[1].adpcm) && now.slice(2).length === L.NFENT);
}
{
  const img = oldBank([["huge", 252000, 11025]]);                         // 131 KB at 11 kHz already: + 86 does not fit
  const dev = new Fake(img);
  const { u, st } = await run(dev, "skip");
  check("3. no room at all: asked (install without them, or cancel), nothing sent",
    u.asked.length === 1 && u.asked[0].choices.join() === "skip,cancel" && st.skipped && dev.sent === 0 &&
    /add them from the samples page/.test(u.said.at(-1)));
}
{
  const img = oldBank([["kick2", 9000, 22050]]);
  const dev = new Fake(img);
  const { u } = await run(dev, "cancel", false);
  const now = L.keptOf(dev.image());
  check("4. no LOOM-8 answering before: looked at after, the set added", u.asked.length === 0 && dev.sent === 1 &&
    now[0].name === "kick2" && now.length === 1 + L.NFENT);
}
{
  const dev = new Fake(null);
  await run(dev, "cancel");
  const sent = dev.sent;
  const { u } = await run(dev, "cancel");
  check("5. the set already there: nothing asked, nothing sent again", sent === 1 && dev.sent === 1 && u.asked.length === 0);
  const full = new Fake(oldBank([["big", 230000, 32000], ["small", 20000, 16000]]));
  const { st } = await run(full, "cancel");
  check("   \"Cancel\": before returns false (the install stops), nothing sent", st === false && full.sent === 0);
}
console.log(bad ? "INSTALL WEB FAILED" : "install web: the factory set added as the installer adds it");
process.exit(bad ? 1 : 0);
