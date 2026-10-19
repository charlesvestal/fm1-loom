// SPDX-License-Identifier: GPL-3.0-only
// LOOM-8's user sample bank in the browser: tools/loom_samples.py and tools/sampleio.py (its numpy path) as plain
// functions, so the samples page (app/web/samples/index.html) builds the bank the command line builds: the same
// layout and header byte for byte, the same sound within a rounding (numpy sums a convolution in its own order).
// tests/host/samples_web.mjs checks it against loom_samples.py. Adapted, as those are, from X0X's sample helpers
// (Felucca's, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments).
//
// Keep the order of operations of sampleio.py when touching these.

export const RATES = [11025, 16000, 22050, 32000];
export const AUTO = [22050, 16000, 11025];
export const BLK = 64;
export const REGIONS = [[0xC1000, 0x1B000], [0xE5000, 0x17000], [0x93000, 0x4000]];
const XDATA = [1, 2], REC_MARK = 0xF06;
export const BANK_SIZE = REGIONS.reduce((a, r) => a + r[1], 0);
export const BANK_HDR = 2048, BANK_MAX_DATA = BANK_SIZE - BANK_HDR, BANK_N = 32, BANK_VER = 2;
export const ENT0 = 64, ENT = 40, BANK_MAGIC = 0x504D534C, NAME_LEN = 12, BARS_MAX = 16;
export const LOOP_ROOT = 53;                  // a loop's slice 1: F3, the lowest key
export const HDR_USED = ENT0 + BANK_N * ENT;  // the header's bytes the FM-1 keeps (1344)
export const PART = 320, NPART = Math.ceil(HDR_USED / PART);
export const READ_MAX = 224;                  // samples.c SB_READ_MAX

const IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
  4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
  18500, 20350, 22385, 24623, 27086, 29794, 32767];
const IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8];

// ---- WAV in (sampleio.read_any_wav): -> {rate, x: mono floats, loop: [start, end] or null}
export function readWav(buf) {
  const d = new DataView(buf.buffer || buf, buf.byteOffset || 0, buf.byteLength);
  const tag4 = (o) => String.fromCharCode(d.getUint8(o), d.getUint8(o + 1), d.getUint8(o + 2), d.getUint8(o + 3));
  if (d.byteLength < 12 || tag4(0) !== "RIFF" || tag4(8) !== "WAVE") throw new Error("not a WAV file");
  const ch = {};
  for (let i = 12; i + 8 <= d.byteLength;) {
    const id = tag4(i), n = d.getUint32(i + 4, true);
    if (!(id in ch)) ch[id] = [i + 8, n];
    i += 8 + n + (n & 1);
  }
  if (!ch["fmt "]) throw new Error("WAV without a fmt chunk");
  const fo = ch["fmt "][0];
  let tag = d.getUint16(fo, true);
  const nch = d.getUint16(fo + 2, true), sr = d.getUint32(fo + 4, true), bits = d.getUint16(fo + 14, true);
  if (tag === 0xFFFE) tag = d.getUint16(fo + 24, true);
  const bps = bits >> 3;
  if ((tag !== 1 && tag !== 3) || !bps || !nch) throw new Error(`unsupported WAV (tag ${tag}, ${bits} bit)`);
  const [o, n] = ch.data || [0, 0];
  const frame = bps * nch, nfr = Math.floor(Math.min(n, d.byteLength - o) / frame);
  const val = (p) => {
    if (tag === 3) return bps === 4 ? d.getFloat32(p, true) : d.getFloat64(p, true);
    if (bps === 2) return d.getInt16(p, true) / 32768;
    if (bps === 3) return ((d.getUint8(p) << 8 | d.getUint8(p + 1) << 16 | d.getUint8(p + 2) << 24) >> 0) / 2147483648;
    if (bps === 4) return d.getInt32(p, true) / 2147483648;
    return (d.getUint8(p) - 128) / 128;
  };
  const x = new Float64Array(nfr);
  for (let f = 0; f < nfr; f++) {
    const p = o + f * frame;
    let acc = 0;                                  // sum(v[f:f + nch]) / nch, from 0, left to right
    for (let c = 0; c < nch; c++) acc += val(p + c * bps);
    x[f] = nch > 1 ? acc / nch : acc;
  }
  let loop = null;
  if (ch.smpl) {
    const so = ch.smpl[0];
    if (so + 52 <= d.byteLength && d.getUint32(so + 28, true)) loop = [d.getUint32(so + 44, true), d.getUint32(so + 48, true)];
  }
  return { rate: sr, x, loop };
}

// numpy.convolve(a, h, mode="same"): length max(N, M), from (min(N, M) - 1) // 2 of the full convolution
function convolveSame(a, h) {
  const N = a.length, M = h.length, L = Math.max(N, M), off = (Math.min(N, M) - 1) >> 1;
  const out = new Float64Array(L);
  for (let i = 0; i < L; i++) {
    const k = i + off;                            // full[k] = sum_j a[k - j] h[j]
    let acc = 0, j0 = Math.max(0, k - N + 1), j1 = Math.min(M - 1, k);
    for (let j = j0; j <= j1; j++) acc += a[k - j] * h[j];
    out[i] = acc;
  }
  return out;
}

function sinc(x) { if (x === 0) return 1.0; const y = Math.PI * x; return Math.sin(y) / y; }
function blackman(M) {                            // numpy.blackman
  const w = new Float64Array(M);
  if (M === 1) { w[0] = 1; return w; }
  for (let i = 0; i < M; i++) {
    const n = 1 - M + 2 * i;
    w[i] = 0.42 + 0.5 * Math.cos(Math.PI * n / (M - 1)) + 0.08 * Math.cos(2.0 * Math.PI * n / (M - 1));
  }
  return w;
}

// sampleio.resample (numpy): down, a windowed-sinc low-pass at 90 % of the new half rate; then linear interpolation
export function resample(x, sr, to) {
  if (sr === to) return Float64Array.from(x);
  let a = Float64Array.from(x);
  if (a.length <= 1) return new Float64Array(0);
  if (to < sr) {
    const fc = 0.45 * to / sr, half = Math.ceil(8.0 / fc), M = 2 * half + 1, w = blackman(M), h = new Float64Array(M);
    let sum = 0;
    for (let i = 0; i < M; i++) { const t = i - half; h[i] = 2.0 * fc * sinc(2.0 * fc * t) * w[i]; }
    for (let i = 0; i < M; i++) sum += h[i];
    for (let i = 0; i < M; i++) h[i] /= sum;
    a = convolveSame(a, h);
  }
  const n = Math.ceil((a.length - 1) * to / sr), step = sr / to, out = new Float64Array(n), last = a.length - 1;
  for (let i = 0; i < n; i++) {                   // numpy.interp on 0, 1, 2 ...
    const p = i * step, j = Math.floor(p);
    out[i] = j >= last ? a[last] : (a[j + 1] - a[j]) * (p - j) + a[j];
  }
  return out;
}

// the pre-emphasis the playback's linear interpolation takes back off (sampleio.EMPHASIS)
const EMPHASIS = [0.0517, -0.21, 1.3362, -0.21, 0.0517].map((v) => v / 1.0196);
export function emphasis(x) { return convolveSame(Float64Array.from(x), EMPHASIS); }

function roundHalfEven(v) {                       // Python's round()
  const f = Math.floor(v), d = v - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return f % 2 === 0 ? f : f + 1;
}

// sampleio.to_int16: the peak normalised to 30000
export function toInt16(x, peakTo = 30000) {
  let pk = 1e-9;
  for (const v of x) { const a = Math.abs(v); if (a > pk) pk = a; }
  const out = new Int16Array(x.length);
  for (let i = 0; i < x.length; i++) out[i] = Math.max(-32768, Math.min(32767, roundHalfEven(x[i] / pk * peakTo)));
  return out;
}

// sampleio.note_from_name: "C4" / "F#3" / "Bb2" in a name, C4 = 60
export function noteFromName(name, def = 60) {
  const m = /(?<![A-Za-z])([A-G])([#b]?)(-?\d)(?!\d)/.exec(name);
  if (!m) return def;
  const pc = "C D EF G A B".indexOf(m[1]) + ({ "#": 1, b: -1, "": 0 })[m[2]];
  return Math.max(0, Math.min(127, (parseInt(m[3], 10) + 1) * 12 + pc));
}

// sampleio.clean_name: lower case, printable, at most 11 characters
export function cleanName(name) {
  const s = name.toLowerCase().replace(/_/g, " ").replace(/[^a-z0-9 _.-]/g, "").trim();
  return (s || "sample").slice(0, NAME_LEN - 1);
}

const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export function noteName(n) { return NOTE[n % 12] + (Math.floor(n / 12) - 1); }

// sampleio.ima_encode: -> {adpcm, ck: Uint32Array of the decoder's state before every BLK-th sample}
export function imaEncode(s) {
  let pred = 0, idx = 0;
  const adpcm = new Uint8Array(((s.length + 1) >> 1)), ck = new Uint32Array(Math.ceil(s.length / BLK));
  for (let n = 0; n < s.length; n++) {
    if (n % BLK === 0) ck[n / BLK] = ((pred & 0xFFFF) | idx << 16) >>> 0;
    const step = IMA_STEP[idx];
    let diff = s[n] - pred, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    let vd = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; vd += step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; vd += step >> 1; }
    if (diff >= step >> 2) { code |= 1; vd += step >> 2; }
    pred = code & 8 ? pred - vd : pred + vd;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += IMA_IDX[code & 7];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    adpcm[n >> 1] |= n & 1 ? code << 4 : code;
  }
  return { adpcm, ck };
}

// the firmware's decoder (dsp/sample.c): for the pictures of samples read back
export function imaDecode(b, n) {
  let pred = 0, idx = 0;
  const out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const code = (b[i >> 1] >> (i & 1 ? 4 : 0)) & 15, step = IMA_STEP[idx];
    let vd = step >> 3;
    if (code & 4) vd += step;
    if (code & 2) vd += step >> 1;
    if (code & 1) vd += step >> 2;
    pred = code & 8 ? pred - vd : pred + vd;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    idx += IMA_IDX[code & 7];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    out[i] = pred;
  }
  return out;
}

export function sampleBytes(n) { return Math.ceil(n / BLK) * 4 + Math.floor((Math.floor((n + 1) / 2) + 3) / 4) * 4; }
export function bytesPerSecond(rate) { return rate * (0.5 + 4.0 / BLK); }

// a file's samples at a rate, ready for the bank (loom_samples.at_rate): {s: Int16Array, loop}
export function atRate(x, sr, loop, rate) {
  const s = toInt16(emphasis(resample(x, sr, rate)));
  const lp = loop ? [Math.trunc(loop[0] * rate / sr), Math.trunc((loop[1] + 1) * rate / sr)] : null;
  return { s, loop: lp };
}
export function lengthAt(nx, sr, rate) { return Math.trunc(nx * rate / sr); }

// ---- the bank's layout (sampleio.user_bank)
function regionStarts() { const out = []; let v = 0; for (const [, n] of REGIONS) { out.push(v); v += n; } return out; }
const STARTS = regionStarts();
function regionOf(v) {
  for (let r = 0; r < REGIONS.length; r++) if (STARTS[r] <= v && v < STARTS[r] + REGIONS[r][1]) return r;
  return REGIONS.length;
}

export class TooLong extends Error {}

function place(blobs, pad) {
  const starts = [...STARTS, BANK_SIZE], out = [];
  let v = BANK_HDR;
  for (let k = 0; k < blobs.length; k++) {
    const [ckb, adpb] = blobs[k];
    v = (v + pad[k] + 3) & ~3;
    let a;
    for (;;) {
      const r = regionOf(v);
      if (r >= REGIONS.length) throw new TooLong("too long");
      if (v + ckb.length > starts[r + 1]) { v = starts[r + 1]; continue; }
      a = v + ckb.length;
      const ra = a < BANK_SIZE ? regionOf(a) : REGIONS.length;
      if (ra >= REGIONS.length || a + adpb.length > starts[Math.min(ra + 2, REGIONS.length)]) {
        if (ra + 1 >= REGIONS.length) throw new TooLong("too long");
        v = starts[ra + 1];
        continue;
      }
      break;
    }
    out.push([v, a]);
    v = a + adpb.length;
  }
  if (v > BANK_SIZE) throw new TooLong("too long");
  return { where: out, end: v };
}

function marks(img, end) {
  const out = [];
  for (const r of XDATA)
    for (let sec = 0; sec < REGIONS[r][1]; sec += 0x1000) {
      const v = STARTS[r] + sec + REC_MARK;
      if (v + 2 <= end && img[v] === 0x41 && img[v + 1] === 0x54) out.push(v);
    }
  return out;
}

let CRC_T = null;
export function crc32(b) {                        // zlib.crc32
  if (!CRC_T) {
    CRC_T = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      CRC_T[n] = c >>> 0;
    }
  }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

// a sample for the bank: {name, n, root, loop: [ls, le] or null, bars, rate} and either s (Int16Array: encoded here)
// or ckb + adpcm (as stored: a sample read back, kept as it is)
function blobOf(smp) {
  if (smp.adpcm) return [smp.ckb, smp.adpcm];
  const { adpcm, ck } = imaEncode(smp.s);
  const ckb = new Uint8Array(ck.length * 4), dv = new DataView(ckb.buffer);
  ck.forEach((c, i) => dv.setUint32(i * 4, c, true));
  return [ckb, adpcm];
}

// sampleio.user_bank: -> the bank's image (header + data, without the padding to BANK_SIZE); throws "too long ..."
export function userBank(samples) {
  if (samples.length < 1 || samples.length > BANK_N) throw new Error(`1..${BANK_N} samples in the bank`);
  const blobs = [], meta = [];
  for (const smp of samples) {
    const n = smp.adpcm ? smp.n : smp.s.length;
    if (!(smp.bars >= 0 && smp.bars <= BARS_MAX)) throw new Error(`${smp.name}: ${smp.bars} bars (0..${BARS_MAX})`);
    if (!(smp.rate >= 4000 && smp.rate <= 48000)) throw new Error(`${smp.name}: ${smp.rate} Hz (4000..48000)`);
    if (!n) throw new Error(`${smp.name}: no audio`);
    blobs.push(blobOf(smp));
    let [ls, le] = smp.loop || [0, 0];
    le = Math.min(le, n);
    if (le <= ls) ls = le = 0;
    meta.push({ nm: cleanName(smp.name), n, ls, le, root: Math.max(0, Math.min(127, smp.root)), bars: smp.bars, rate: smp.rate });
  }
  const pad = new Array(blobs.length).fill(0);
  let where, end, img = null;
  for (let t = 0; ; t++) {
    if (t >= 4096) throw new Error("could not keep the update record's mark out of the bank");
    try { ({ where, end } = place(blobs, pad)); } catch (e) {
      if (e instanceof TooLong) {
        const used = blobs.reduce((a, [c, d]) => a + c.length + d.length, 0);
        throw new TooLong(`too long: ${used} B, the bank holds ${BANK_MAX_DATA} B`);
      }
      throw e;
    }
    img = new Uint8Array(end).fill(0xFF);
    blobs.forEach(([ckb, adpb], k) => { img.set(ckb, where[k][0]); img.set(adpb, where[k][1]); });
    const mk = marks(img, end);
    if (!mk.length) break;
    const k = where.findIndex(([c, a], i) => c <= mk[0] && mk[0] < a + blobs[i][1].length);
    if (k < 0) throw new Error("an update record's mark outside the samples");
    pad[k] += 4;
  }
  const data = img.subarray(BANK_HDR);
  const hdr = new Uint8Array(BANK_HDR), v = new DataView(hdr.buffer);
  v.setUint32(0, BANK_MAGIC, true);
  v.setUint16(4, BANK_VER, true);
  hdr[6] = meta.length;
  hdr[7] = 0;
  v.setUint32(8, data.length, true);
  v.setUint32(12, crc32(data), true);
  v.setUint32(16, BANK_SIZE, true);
  meta.forEach((m, k) => {
    const o = ENT0 + k * ENT;
    for (let i = 0; i < m.nm.length; i++) hdr[o + i] = m.nm.charCodeAt(i);
    v.setUint32(o + 12, where[k][1], true);         // the ADPCM, then the checkpoints
    v.setUint32(o + 16, where[k][0], true);
    v.setUint32(o + 20, m.n, true);
    v.setUint32(o + 24, m.ls, true);
    v.setUint32(o + 28, m.le, true);
    hdr[o + 32] = m.root;
    hdr[o + 34] = m.bars;
    v.setUint16(o + 36, m.rate, true);
  });
  const out = new Uint8Array(BANK_HDR + data.length);
  out.set(hdr);
  out.set(data, BANK_HDR);
  return out;
}

// a bank's header -> its samples' entries (null: not a bank)
export function parseHeader(hdr) {
  const v = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
  if (hdr.length < ENT0 || v.getUint32(0, true) !== BANK_MAGIC || v.getUint16(4, true) !== BANK_VER) return null;
  const count = hdr[6], out = [];
  if (count > BANK_N || hdr.length < ENT0 + count * ENT) return null;
  for (let k = 0; k < count; k++) {
    const o = ENT0 + k * ENT;
    let name = "";
    for (let i = 0; i < NAME_LEN && hdr[o + i]; i++) name += String.fromCharCode(hdr[o + i]);
    out.push({ name, adpOff: v.getUint32(o + 12, true), ckOff: v.getUint32(o + 16, true), n: v.getUint32(o + 20, true),
      ls: v.getUint32(o + 24, true), le: v.getUint32(o + 28, true), root: hdr[o + 32], bars: hdr[o + 34],
      rate: v.getUint16(o + 36, true) });
  }
  return { count, dataLen: v.getUint32(8, true), crc: v.getUint32(12, true), entries: out };
}

// loom_samples.pack7: groups of up to 7 bytes, each preceded by their top bits; and back
export function pack7(b) {
  const out = [];
  for (let i = 0; i < b.length; i += 7) {
    let m = 0;
    const g = b.subarray ? b.subarray(i, i + 7) : b.slice(i, i + 7);
    g.forEach((x, j) => { m |= ((x >> 7) & 1) << j; });
    out.push(m);
    for (const x of g) out.push(x & 0x7F);
  }
  return out;
}
export function unpack7(a) {
  const out = [];
  for (let i = 0; i < a.length;) {
    const m = a[i++];
    for (let j = 0; j < 7 && i < a.length; j++) out.push(a[i++] | ((m >> j) & 1) << 7);
  }
  return new Uint8Array(out);
}

// loom_samples.frames: the upload's frames, [command, args, timeout ms, what]
export function* frames(image) {
  yield [11, [0], 10000, "begin"];
  for (let off = BANK_HDR; off < image.length; off += 256)
    yield [12, [0, off & 0x7F, (off >> 7) & 0x7F, (off >> 14) & 0x7F, ...pack7(image.subarray(off, off + 256))], 5000, off];
  for (let part = 0; part < NPART; part++)
    yield [16, [0, part, ...pack7(image.subarray(0, BANK_HDR).subarray(part * PART, (part + 1) * PART))], 3000, `header part ${part}`];
  yield [13, [0], 20000, "end"];
}

// loom_samples.build: the bank from items in order, each either kept ({name, n, root, loop, bars, rate, ckb, adpcm}:
// read back) or new ({name, x, sr, loop (the file's), root, bars, rate: its own or null}); rate: for the new ones that
// did not choose (null: auto, the highest of AUTO at which all fits). -> {img, chose}; throws TooLong.
// cache (a Map) keeps each new item's samples at a rate between calls.
export function build(items, rate = null, cache = new Map()) {
  if (items.length < 1 || items.length > BANK_N) throw new Error(`1..${BANK_N} samples in the bank`);
  const tries = rate ? [rate] : AUTO;
  for (const r of tries) {
    const est = items.reduce((a, it) => a + sampleBytes(it.adpcm ? it.n : lengthAt(it.x.length, it.sr, it.rate || r)), 0);
    if (est > BANK_MAX_DATA) continue;
    const smps = items.map((it) => {
      if (it.adpcm) return it;
      const rr = it.rate || r, key = rr;
      let c = cache.get(it);
      if (!c) cache.set(it, c = new Map());
      if (!c.has(key)) c.set(key, atRate(it.x, it.sr, it.loop, rr));
      const { s, loop } = c.get(key);
      return { name: it.name, s, root: it.root, loop, bars: it.bars, rate: rr };
    });
    try { return { img: userBank(smps), chose: r }; } catch (e) { if (!(e instanceof TooLong)) throw e; }
  }
  const last = tries[tries.length - 1];
  const need = items.reduce((a, it) => a + sampleBytes(it.adpcm ? it.n : lengthAt(it.x.length, it.sr, it.rate || last)), 0);
  throw new TooLong(`too long: ${(need / 1024).toFixed(1)} KB at ${last} Hz, the bank holds ${(BANK_MAX_DATA / 1024).toFixed(1)} KB`);
}

// the samples of a bank image as kept items (a bank read back: each sample's checkpoints and ADPCM as stored)
export function keptOf(img) {
  const h = parseHeader(img.subarray(0, Math.min(BANK_HDR, img.length)));
  if (!h) return [];
  return h.entries.map((e) => ({
    name: e.name, n: e.n, root: e.root, loop: e.le > e.ls ? [e.ls, e.le] : null, bars: e.bars, rate: e.rate,
    ckb: img.slice(e.ckOff, e.ckOff + Math.ceil(e.n / BLK) * 4), adpcm: img.slice(e.adpOff, e.adpOff + ((e.n + 1) >> 1)),
  }));
}
