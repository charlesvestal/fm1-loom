// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// LOOM-8's preset packs in the browser: a pack is a run of USER_PUT messages (app/samples.c: F0 7D 46 4C 1D slot fmt
// fmt data F7, a user preset as LOOM-8 saves it, 8 bytes packed from 7), as tools/loom_presets.py makes them. The
// presets page reads a pack, the FM-1's preset names (USER_NAMES, 30) and sends the presets (USER_PUT, 29) through the
// samples page's Link (loom_samples.js). LOOM-8 takes any presets format it can load and makes it its own; a newer one
// it refuses (rc 9).
export const USER_PUT = 0x1D, USER_NAMES = 0x1E, NUSER = 64, PNAME_LEN = 11;
export const PUT_RC = { 1: "no such slot, or the wrong size", 3: "the FM-1 could not save it",
  5: "not a sound LOOM-8 knows", 9: "made for a newer LOOM-8: update it first" };
export function packUnpack(a) {
  const out = [];
  for (let g = 0; g < a.length; g += 8)
    for (let k = 1; k < 8 && g + k < a.length; k++) out.push(a[g + k] | ((a[g] >> (k - 1)) & 1) << 7);
  return out;
}
// a pack's bytes -> [{slot (0-based, as made), fmt, data (packed, as sent), name}]; [] when it holds none
export function parsePack(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] !== 0xF0) continue;
    const j = bytes.indexOf(0xF7, i);
    if (j < 0) break;
    const m = bytes.subarray(i + 1, j);
    if (m.length > 7 && m[0] === 0x7D && m[1] === 0x46 && m[2] === 0x4C && m[3] === USER_PUT) {
      const data = [...m.subarray(7)], raw = packUnpack(data);
      let name = "";
      for (let k = 0; k < PNAME_LEN && raw[k]; k++) name += String.fromCharCode(raw[k]);
      out.push({ slot: m[4], fmt: m[5] | m[6] << 7, data, name: name.trim() || "(no name)" });
    }
    i = j;
  }
  return out;
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
// one preset into slot s: 0, or LOOM-8's rc
export async function putPreset(link, s, p) {
  const r = await link.req(USER_PUT, [s, p.fmt & 127, (p.fmt >> 7) & 127, ...p.data], 5000);
  return r.length > 1 ? r[1] : 99;
}
