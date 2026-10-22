// SPDX-License-Identifier: GPL-3.0-only
// LOOM-8's sample bank as a section of the backup file (fm1-base's web/backup page, EXTRAS; tag "LSMP"): read back
// through SMP_READ (17), written as the samples page writes it (BEGIN 11, WRITE 12, HDR 16, END 13; ERASE 14 for a
// bank with no samples), app/samples.c. The section is the bank's image as tools/loom_samples.py backup / extract
// make it (the 2048-byte header, then the data; a header alone with a count of 0 for an empty bank), so
// "loom_samples.py restore" takes the file too. tools/make_pages.py inlines it after app/web/samples/loom_samples.js,
// both inside one function that returns EXTRAS; tests/host/backup_web.mjs runs it against a simulated FM-1.
const SB = [0x7D, 0x46, 0x4C];
const SB_RC = { 1: "size or offset", 2: "erase", 3: "write", 4: "not begun", 5: "header", 6: "the data's check",
  7: "no samples", 8: "an update record's mark" };

// one request, its answer (the bytes after the command; one that is(r) accepts: a read's or a write's own offset, not
// a late answer to an earlier one) or an error; link: fm1-base's backup page Link
async function sbReq(link, cmd, args, ms = 3000, is = () => true) {
  link.send([0xF0, ...SB, cmd, ...args, 0xF7]);
  const end = Date.now() + ms;
  for (;;) {
    const left = end - Date.now();
    if (left <= 0 || link.lost) throw new Error("LOOM-8 stopped answering.");
    const p = await link.read(left);
    if (p && p.length > 5 && p[1] === SB[0] && p[2] === SB[1] && p[3] === SB[2] && p[4] === cmd && is(p.slice(5, -1))) return p.slice(5, -1);
  }
}
const off3 = (o) => [o & 0x7F, (o >> 7) & 0x7F, (o >> 14) & 0x7F];
const sameOff = (r, o) => r[1] === (o & 0x7F) && r[2] === ((o >> 7) & 0x7F) && r[3] === ((o >> 14) & 0x7F);

// n bytes of the bank from virtual offset off (224 a request; a lost answer asked again)
async function sbRead(link, off, n, onPart) {
  const out = new Uint8Array(n);
  for (let at = 0; at < n;) {
    const want = Math.min(READ_MAX, n - at), adr = off + at;
    let r = null;
    for (let k = 1; !r; k++) {
      try { r = await sbReq(link, 17, [0, ...off3(adr), want & 0x7F, want >> 7], 3000, (a) => sameOff(a, adr)); }
      catch (e) { if (k >= 4) throw e; }
    }
    if (r[4]) throw new Error(r[4] === 4 ? "LOOM-8 is in the middle of writing its samples. Try again when it's done."
                                         : `LOOM-8 couldn't read its samples (${SB_RC[r[4]] || r[4]}).`);
    const b = unpack7(r.slice(5));
    if (b.length < want) throw new Error("A piece of the samples went missing on the way. Press Back up again.");
    out.set(b.subarray(0, want), at);
    at += want;
    if (onPart) onPart(at / n);
  }
  return out;
}

function emptyBank() {
  const hdr = new Uint8Array(BANK_HDR), v = new DataView(hdr.buffer);
  v.setUint32(0, BANK_MAGIC, true);
  v.setUint16(4, BANK_VER, true);
  v.setUint32(16, BANK_SIZE, true);
  return hdr;
}

// a section's bank image checked -> {count, used, img} (throws in plain words)
function bankOf(data) {
  const h = data.length >= BANK_HDR ? parseHeader(data.subarray(0, BANK_HDR)) : null;
  if (!h || h.dataLen > BANK_MAX_DATA || data.length !== BANK_HDR + h.dataLen)
    throw new Error("The samples in this backup are damaged.");
  if (crc32(data.subarray(BANK_HDR)) !== h.crc) throw new Error("The samples in this backup are damaged (a byte changed).");
  return { count: h.count, used: h.dataLen, img: data };
}

const LOOM_SAMPLES = {
  tag: "LSMP",
  what: "the samples",
  async backup(link, onProgress = () => {}) {
    const info = await sbReq(link, 15, [0]);
    if (info[0] !== 2) throw new Error("This LOOM-8 is too old to back up its samples. Update it from the Install page.");
    if (!info[5]) { onProgress(1); return emptyBank(); }
    const hdr = await sbRead(link, 0, BANK_HDR);
    const h = parseHeader(hdr);
    if (!h || h.dataLen > BANK_MAX_DATA) throw new Error("LOOM-8's samples could not be read (their header).");
    const data = await sbRead(link, BANK_HDR, h.dataLen, onProgress);
    if (crc32(data) !== h.crc) throw new Error("The samples arrived damaged. Press Back up again.");
    const img = new Uint8Array(BANK_HDR + data.length);
    img.set(hdr);
    img.set(data, BANK_HDR);
    return img;
  },
  check(data) {
    const b = bankOf(data);
    return b.count ? `${b.count} sample${b.count === 1 ? "" : "s"} (${Math.round(b.used / 1024)} KB)` : "no samples";
  },
  async restore(link, data, onProgress = () => {}) {
    const b = bankOf(data);
    if (!b.count) {
      const r = await sbReq(link, 14, [0], 60000);
      if (r[1]) throw new Error(`LOOM-8 couldn't erase its samples (${SB_RC[r[1]] || r[1]}).`);
      onProgress(1);
      return;
    }
    const total = b.img.length - BANK_HDR;
    for (const [cmd, args, ms, what] of frames(b.img)) {
      let r = null;
      for (let k = 1; !r; k++) {                     // a write again when its answer is lost (the same bytes)
        try { r = await sbReq(link, cmd, args, ms, (a) => cmd !== 12 || sameOff(a, what)); }
        catch (e) { if (cmd !== 12 || k >= 4) throw e; }
      }
      const rc = r.length ? r[r.length - 1] : 99;
      if (rc) throw new Error(`LOOM-8 didn't take the samples (${SB_RC[rc] || rc}).`);
      if (cmd === 12) onProgress((Math.min(what + 256, b.img.length) - BANK_HDR) / total);
    }
    const info = await sbReq(link, 15, [0]);
    if (info[5] !== b.count) throw new Error(`LOOM-8 shows ${info[5]} samples, not ${b.count}.`);
  },
};
return [LOOM_SAMPLES];
