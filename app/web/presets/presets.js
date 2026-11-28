// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// LOOM-8's preset packs in the browser: a pack is a run of USER_PUT messages (app/samples.c: F0 7D 46 4C 1D slot fmt
// fmt data F7, a user preset as LOOM-8 saves it, 8 bytes packed from 7), as tools/loom_presets.py makes them. The
// presets page reads a pack, the FM-1's preset names (USER_NAMES, 30) and sends the presets (USER_PUT, 29) through the
// samples page's Link (loom_samples.js). LOOM-8 takes any presets format it can load and makes it its own; a newer one
// it refuses (rc 9).
export const USER_PUT = 0x1D, USER_NAMES = 0x1E, KIT_PUT = 0x1F, USER_GET = 0x20, KIT_GET = 0x21, KIT_NAMES = 0x22;
export const NUSER = 64, NKITS = 8, PNAME_LEN = 11, KIT_LEN = 321, KIT_LEN16 = 315;
export const PUT_RC = { 1: "no such slot, or the wrong size", 3: "the FM-1 could not save it",
  5: "not a sound or kit LOOM-8 knows", 9: "made for a newer LOOM-8: update it first" };
export function packUnpack(a) {
  const out = [];
  for (let g = 0; g < a.length; g += 8)
    for (let k = 1; k < 8 && g + k < a.length; k++) out.push(a[g + k] | ((a[g] >> (k - 1)) & 1) << 7);
  return out;
}
export function packBytes(b) {                    // loom_presets.py pack7: a byte of eighth bits, then up to seven
  const out = [];
  for (let g = 0; g < b.length; g += 7) {
    const run = b.slice(g, g + 7);
    out.push(run.reduce((h, x, k) => h | (x >> 7) << k, 0), ...run.map((x) => x & 127));
  }
  return out;
}
function nameOf(raw) {
  let name = "";
  for (let k = 0; k < PNAME_LEN && raw[k]; k++) name += String.fromCharCode(raw[k]);
  return name.trim();
}
// a user preset (USER_PUT) or a kit (KIT_PUT) as a pack's message
export function frame(kind, slot, fmt, raw) {
  return [0xF0, 0x7D, 0x46, 0x4C, kind, slot & 127, fmt & 127, (fmt >> 7) & 127, ...packBytes(raw), 0xF7];
}
// a pack's bytes -> [{kind (USER_PUT, KIT_PUT), slot (0-based, as made), fmt, data (packed, as sent), name}]
export function parsePack(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] !== 0xF0) continue;
    const j = bytes.indexOf(0xF7, i);
    if (j < 0) break;
    const m = bytes.subarray(i + 1, j);
    if (m.length > 7 && m[0] === 0x7D && m[1] === 0x46 && m[2] === 0x4C && (m[3] === USER_PUT || m[3] === KIT_PUT)) {
      const data = [...m.subarray(7)];
      out.push({ kind: m[3], slot: m[4], fmt: m[5] | m[6] << 7, data, name: nameOf(packUnpack(data)) || "(no name)" });
    }
    i = j;
  }
  return out;
}
// a user preset's bytes in presets format fmt (app/project.c preset_len)
export function presetLen(f) {
  return f >= 20 ? 93 : f >= 17 ? 79 : f >= 16 ? 69 : f >= 15 ? 64 : f >= 14 ? 63 : f >= 13 ? 59 : f >= 12 ? 55 : f >= 9 ? 53 : 52;
}
const u32 = (b, i) => (b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24) >>> 0;
const u16 = (b, i) => b[i] | b[i + 1] << 8;
// a LOOM-8 backup (.loom8, any version) -> its user presets and kits as a pack's items (as parsePack's), each in the
// format it was saved in (LOOM-8 makes an older one its own when it takes it): loom_presets.py backup_items
export function parseBackup(b) {
  if (b[0] !== 0x4C || b[1] !== 0x4D || b[2] !== 0x38 || b[3] !== 0x42) return null;
  const objs = new Map(), n = u16(b, 6);
  for (let i = 0, pos = 16; i < n && pos + 8 <= b.length; i++) {
    const id = u16(b, pos), len = u16(b, pos + 2);
    objs.set(id, b.subarray(pos + 8, pos + 8 + len));
    pos += 8 + len;
  }
  const out = [], add = (kind, fmt, raw) => {
    if (raw.length && raw[0]) out.push({ kind, slot: out.filter((x) => x.kind === kind).length & 127, fmt,
                                        data: packBytes([...raw]), name: nameOf(raw) || "(no name)" });
  };
  const pr = objs.get(1) || new Uint8Array(0), pf = pr.length >= 4 ? u32(pr, 0) : 0;
  if (pf >= 6 && pf <= 18)                       // (the 16 presets before 19)
    for (let k = 0, pl = presetLen(pf); k < 16 && 4 + (k + 1) * pl <= pr.length; k++) add(USER_PUT, pf, pr.subarray(4 + k * pl, 4 + (k + 1) * pl));
  for (const id of [23, 24]) {                    // (OBJ_UP0, OBJ_UP1)
    const u = objs.get(id);
    if (!u || u.length < 4) continue;
    const f = u32(u, 0), pl = presetLen(f);
    for (let k = 0; k < 32 && 4 + (k + 1) * pl <= u.length; k++) add(USER_PUT, f, u.subarray(4 + k * pl, 4 + (k + 1) * pl));
  }
  const [kat, kl] = pf >= 19 ? [4, KIT_LEN] : pf >= 17 ? [4 + 16 * 79, KIT_LEN] : pf >= 11 ? [4 + 16 * presetLen(pf), KIT_LEN16] : [0, 0];
  for (let k = 0; kat && k < NKITS && kat + (k + 1) * kl <= pr.length; k++) add(KIT_PUT, pf, pr.subarray(kat + k * kl, kat + (k + 1) * kl));
  return out;
}
// the user kits' names on the FM-1 (8: "" an empty one)
export async function readKitNames(link) {
  const r = await link.retry(KIT_NAMES, [], 2000), names = [];
  for (let i = 0, p = 1; i < r[0]; i++) {
    let s = "";
    while (p < r.length && r[p]) s += String.fromCharCode(r[p++]);
    p++;
    names.push(s);
  }
  return names;
}
// the FM-1's user presets and kits as a pack's bytes (USER_GET, KIT_GET); onStep(done, of) as it reads
export async function exportPack(link, onStep) {
  const out = [];
  let done = 0;
  for (const [kind, get, n] of [[USER_PUT, USER_GET, NUSER], [KIT_PUT, KIT_GET, NKITS]])
    for (let s = 0; s < n; s++) {
      const r = await link.retry(get, [s], 3000);
      if (r.length > 4 && r[1] === 0) out.push(...frame(kind, s, r[2] | r[3] << 7, packUnpack(r.slice(4))));
      if (onStep) onStep(++done, NUSER + NKITS);
    }
  return new Uint8Array(out);
}
// the user presets' names on the FM-1 (64: "" an empty slot)
export async function readNames(link) {
  const names = [];
  for (let from = 0; from < NUSER;) {
    const r = await link.retry(USER_NAMES, [from], 2000);
    const k = r[1];
    let p = 3;
    for (let i = 0; i < k; i++) {
      let s = "";
      while (p < r.length && r[p]) s += String.fromCharCode(r[p++]);
      p++;
      names.push(s);
    }
    if (!k) break;
    from += k;
  }
  return names;
}
// where a pack's presets go: from first (0-based) on, in order; the slots past U64 none
export function placeFrom(pack, first) {
  return pack.map((p, i) => (first + i < NUSER ? first + i : -1));
}
// the first slot with room for n presets in a row among the empty ones; -1: none
export function firstRoom(names, n) {
  for (let s = 0; s + n <= NUSER; s++) if (names.slice(s, s + n).every((x) => !x)) return s;
  return -1;
}
// one preset (or a kit: p.kind KIT_PUT) into slot s: 0, or LOOM-8's rc
export async function putPreset(link, s, p) {
  const r = await link.req(p.kind || USER_PUT, [s, p.fmt & 127, (p.fmt >> 7) & 127, ...p.data], 5000);
  return r.length > 1 ? r[1] : 99;
}
