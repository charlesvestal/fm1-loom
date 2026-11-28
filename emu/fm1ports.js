// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Chris Farrell (the FM-1's usual port names, and other ports asked only once allowed: from
//   fm-static, github.com/seajaysec/fm-static, by his permission under MPL-2.0: seajaysec/fm-static#1)
//
// Finding FM-1s on Web MIDI, one copy for every page: fm1-base's installer (through fm1ota.js) and backup page, and
// the FM-1 Screen Reader's page (fm1-reader web/reader.js). fm1-reader keeps a copy of this file, made and checked by
// its tools/sync_announce.sh: change it here.
//
// A port is asked who it is in one of two ways:
//  - The screen reader's HELLO (fm1-reader PROTOCOL.md): F0 7D 53 52 01 01 F7. 7D is the ID for non-commercial use,
//    which other gear ignores, so a HELLO goes to every port, ASK_TRIES times while the port stays plugged in. Every
//    firmware on fm1-base answers it (device/announce.c) with its name and the link to its words file, whatever its
//    port is called. M-VAVE's firmware and the update loader don't.
//  - The update protocol's identity query (fm1ota.js handshake): M-VAVE's own SysEx, which the stock firmware and the
//    loader answer too. It goes only to an askable port: one with a usual name (PORT_NAMES), one the user picked, or
//    one that answered a HELLO.
// A HELLO answer is told from the HELLO itself by the name in it: a loopback (macOS's IAC bus, a DAW's MIDI through)
// sends the HELLO back as it went out, with no name.

// port names asked without being picked (any case, part of the name). "FM1": the Open firmware's "FM1 Community"
// The FM-1 firmwares of the family name their own port after themselves (an FM-1 running one shows as that name alone):
// "optimist", "sloop", "melodee", "loom-8" / "loom8", as tools/fm1ports.py asks them too.
export const PORT_NAMES = ["fm-1", "fm1", "felucca", "ota", "composite", "sinco", "usb-midi", "optimist", "sloop", "melodee", "loom-8", "loom8"];
export function usualName(name, names = PORT_NAMES) {
  const n = (name || "").toLowerCase();
  return names.some((p) => n.includes(p));
}

// never a Bluetooth MIDI port: an FM-1 paired over Bluetooth shows one beside its USB port, an install or a backup over
// it is not offered, and it answers nothing asked of it. bluetooth = true: a list the user picks from by hand (the mirror
// page's two drop-downs) keeps them, after the others
export const IS_BLUETOOTH = (name) => /bluetooth/i.test(name || "");
export const livePorts = (map, bluetooth = false) => {
  const live = [...map.values()].filter((p) => p.state !== "disconnected" && (bluetooth || !IS_BLUETOOTH(p.name)));
  return bluetooth ? [...live.filter((p) => !IS_BLUETOOTH(p.name)), ...live.filter((p) => IS_BLUETOOTH(p.name))] : live;
};

// an input's output: the one with its name; else the only one with its name but for a number at the end (Windows
// numbers each port); else the only output there is. null: none. A Bluetooth input (picked by hand) looks among the
// Bluetooth outputs too
export function outputFor(access, input) {
  const outs = livePorts(access.outputs, IS_BLUETOOTH(input.name));
  const bare = (s) => (s || "").replace(/\s+\d+$/, "");
  const same = outs.filter((o) => bare(o.name) === bare(input.name));
  return outs.find((o) => o.name === input.name) || (same.length === 1 ? same[0] : null) ||
    (outs.length === 1 ? outs[0] : null);
}

// every plugged-in input that has an output: [{ name, input, output }]
export function pairs(access) {
  return livePorts(access.inputs).map((input) => ({ name: input.name || "", input, output: outputFor(access, input) }))
    .filter((p) => p.output);
}

// ---------------------------------------------------------------- the screen reader's HELLO (fm1-reader PROTOCOL.md)
export const SR = [0x7D, 0x53, 0x52];
export const SR_HELLO = 1, SR_BYE = 2, SR_WHERE = 3, SR_SAY = 16, SR_VER = 1;
export const ASK_TRIES = 3;
export const srFrame = (cmd, ...args) => [0xF0, ...SR, cmd, ...args, 0xF7];
export const isSr = (d) => !!d && d[0] === 0xF0 && d[1] === SR[0] && d[2] === SR[1] && d[3] === SR[2];

// a HELLO answer -> { name, words } (words: the https link to its words file, or ""), or null when it is none: a
// HELLO sent back by a loopback has no name
export function srHello(d) {
  if (!isSr(d) || d[4] !== SR_HELLO || d[d.length - 1] !== 0xF7) return null;
  const [name = "", words = ""] = String.fromCharCode(...Array.from(d.slice(6, d.length - 1)).filter((b) => b < 128))
    .split("\0");
  if (!name) return null;
  return { name, words: /^https:\/\/[^\s"']+\.json$/.test(words) ? words : "" };
}

// which outputs a HELLO goes to now: every output ASK_TRIES times after it appears, then only the ones that answered
// (a reader keeps those listening). Other gear on the computer gets three short SysEx it ignores, not one every two
// seconds. forget(id) when a port is unplugged or lost: it is asked again
export class Asking {
  constructor(tries = ASK_TRIES) { this.tries = tries; this.n = new Map(); }
  due(output, answered = false) {
    const k = this.n.get(output.id) || 0;
    if (!answered && k >= this.tries) return false;
    this.n.set(output.id, k + 1);
    return true;
  }
  forget(id) { this.n.delete(id); }
  clear() { this.n.clear(); }
}

// one search: the port pairs that answer a HELLO -> [{ name, firmware, words, input, output }] (firmware: the name
// and version it gives, e.g. "X0X 1.1"). Asks up to `tries` times, `ms` apart, stopping when every port has answered;
// skip(name): ports not to ask. No BYE after it: a device stops by itself 5 s after the last HELLO, and a BYE would
// also stop a reader page that is listening to it
export async function askAll(access, { ms = 500, tries = ASK_TRIES, skip = () => false } = {}) {
  const ps = pairs(access).filter((p) => !skip(p.name));
  const got = new Map();
  const ears = ps.map((p) => [p, (e) => {
    const h = srHello(e.data);
    if (h && !got.has(p.input)) got.set(p.input, { name: p.name, firmware: h.name, words: h.words, input: p.input, output: p.output });
  }]);
  for (const [p, f] of ears) {
    try { await p.input.open(); } catch (_) { /* taken: it answers nothing */ }
    p.input.addEventListener("midimessage", f);
  }
  try {
    for (let t = 0; t < tries && got.size < ps.length; t++) {
      const sent = new Set();
      for (const p of ps) {
        if (got.has(p.input) || sent.has(p.output)) continue;
        sent.add(p.output);
        try { p.output.send(srFrame(SR_HELLO, SR_VER)); } catch (_) { /* unplugged */ }
      }
      await new Promise((r) => setTimeout(r, ms));
    }
  } finally {
    for (const [p, f] of ears) p.input.removeEventListener("midimessage", f);
  }
  return [...got.values()];
}
