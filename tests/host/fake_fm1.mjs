// SPDX-License-Identifier: MPL-2.0
// A simulated FM-1 running LOOM-8, for the web pages' tests: the sample bank's commands as app/samples.c answers them
// (INFO 15, READ 17, BEGIN 11, WRITE 12, HDR 16, END 13, ERASE 14), on the backup page's link (send, read(ms), drain,
// lost); answers can be lost (drop) or late (late), and a command can be refused (rc). L: app/web/samples/loom_samples.js's
// functions (BANK_HDR, BANK_SIZE, parseHeader, pack7, unpack7, crc32). tests/host/backup_web.mjs and export_web.mjs.
export class FakeFM1 {
  constructor(L, img, { drop = [], late = [], rc = {} } = {}) {
    this.L = L;
    this.bank = new Uint8Array(this.L.BANK_SIZE).fill(0xFF);
    if (img) this.bank.set(img);
    this.q = []; this.n = 0; this.drop = drop; this.late = late; this.rc = rc; this.open = false; this.hdr = null;
    this.writes = 0; this.lost = false;
  }
  count() { const h = this.L.parseHeader(this.bank.subarray(0, this.L.BANK_HDR)); return h ? h.count : 0; }
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
      return [0, ...a.slice(1, 4), 0, ...this.L.pack7(this.bank.subarray(o, o + n))];
    }
    if (cmd === 11) { this.open = true; this.hdr = new Uint8Array(this.L.BANK_HDR); this.bank.fill(0xFF, 0, 4096); return [0, this.rc[11] || 0]; }
    if (cmd === 12) {
      const o = off(1), d = this.L.unpack7(a.slice(4));
      if (!this.open) return [0, ...a.slice(1, 4), 4];
      this.bank.set(d.subarray(0, 256), o); this.writes++;
      return [0, ...a.slice(1, 4), this.rc[12] || 0];
    }
    if (cmd === 16) { this.hdr.set(this.L.unpack7(a.slice(2)).subarray(0, 320), a[1] * 320); return [0, a[1], 0]; }
    if (cmd === 13) {
      const h = this.L.parseHeader(this.hdr);
      const ok = h && this.L.crc32(this.bank.subarray(this.L.BANK_HDR, this.L.BANK_HDR + h.dataLen)) === h.crc;
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

