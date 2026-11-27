// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Juno-106 and Prophet-5 (Rev 4) patches as LOOM-8 knit presets, in the browser: tools/juno_import.py's and
// tools/prophet_import.py's convert(), line for line (Python's rounding too: the same values; tests/host/convert_web.mjs
// checks every patch of a bank against them), and a preset's bytes made from their parameters as LOOM-8 makes one
// (the tables, T: the simulator's webtables, tools/make_pages.py: knit's defaults, the layout, the names). The curves and
// what each control becomes are the tools' (and their sources: Saadet's, Kerem Kilic, MPL-2.0).

// ---- Python's arithmetic
const pyRound = (x) => (Math.abs(x % 1) === 0.5 ? 2 * Math.round(x / 2) : Math.round(x));
const clamp = (v, lo = 0, hi = 127) => Math.max(lo, Math.min(hi, pyRound(v)));
const log2 = Math.log2;
export const OCT_NAME = ["-2", "-1", "0", "+1", "+2"];

// ---- knit's side (juno_import.py)
const J_F0 = 5.528709, J_OCT = 1143.0;
const jCutHz = (v) => J_F0 * 2.0 ** (v * 128.0 / J_OCT);
const knitCut = (hz) => clamp(log2(Math.max(hz, 1e-3) / 50.0) * 127.0 / 8.64385619);
const jTime = (v, most) => { const x = v / 127.0; return 0.0015 + most * x * x; };
const knitTime = (secs) => clamp(log2(Math.max(secs, 0.001) / 0.001) * 127.0 / 13.2877124, 0, 146);

// ---- Juno-106: F0 41 30 0n pp, 16 sliders, SW1, SW2, F7
const [LFO_RATE, LFO_DELAY, DCO_LFO, DCO_PWM, NOISE, VCF_FREQ, VCF_RES, VCF_ENV, VCF_LFO, VCF_KYBD, VCA_LEVEL, ENV_A,
  ENV_D, ENV_S, ENV_R, SUB, SW1, SW2] = [...Array(18).keys()];
export const junoLabel = (k) => `${k & 64 ? "B" : "A"}${1 + (k >> 3 & 7)}${1 + (k & 7)}`;
// a bank's bytes -> Map(program -> 18 bytes); empty when it holds none
export function readJuno(b) {
  const out = new Map();
  for (let i = 0; i + 24 <= b.length; i++) {
    if (b[i] !== 0xF0 || b[i + 1] !== 0x41 || b[i + 2] !== 0x30) continue;
    const m = b.subarray(i, i + 24);
    if (m[23] === 0xF7 && (m[3] & 0xF0) === 0 && [...m.subarray(4, 23)].every((x) => x < 128)) {
      out.set(m[4], m.slice(5, 23));
      i += 23;
    }
  }
  return out;
}
export function convertJuno(p) {
  const sw1 = p[SW1], sw2 = p[SW2];
  const rng = sw1 & 1 ? 0 : sw1 & 2 ? 1 : sw1 & 4 ? 2 : 1;
  const pulse = !!(sw1 & 8), saw = !!(sw1 & 16);
  const chorus = sw1 & 32 ? 0 : sw1 & 64 ? 1 : 2;
  const pwmManual = !!(sw2 & 1), envInv = !!(sw2 & 2), gate = !!(sw2 & 4);
  const hpf = 3 - ((sw2 >> 3) & 3);
  const notes = [], out = [["engine", "knit"]];
  const pwm = p[DCO_PWM];
  if (pulse) out.push(["e1", clamp(pwmManual ? 64 + pwm * 0.5 : 64 + pwm * 0.25)]);
  else out.push(["e1", 0]);
  out.push(["e2", 0], ["e3", 1], ["e4", 0], ["x1", 64], ["fine", 64], ["kanalog", 0]);
  if (pulse && saw) out.push(["o2wave", "saw"], ["x2", 127]);
  else out.push(["o2wave", "same"], ["x2", 0]);
  if (!pulse && !saw) notes.push("no saw or pulse: knit's osc 1 cannot be turned off, its saw plays beside the sub and noise");
  out.push(["x3", p[SUB]], ["ksub", 1], ["x4", clamp(p[NOISE] * 0.8)]);
  const amp = gate ? [0, 0, 127, 0] : [knitTime(jTime(p[ENV_A], 3.0)), knitTime(jTime(p[ENV_D], 12.0) * 1.5), p[ENV_S],
    knitTime(jTime(p[ENV_R], 12.0) * 1.5)];
  out.push(["a", amp[0]], ["d", amp[1]], ["s", amp[2]], ["r", amp[3]]);
  out.push(["fa", knitTime(jTime(p[ENV_A], 3.0))], ["fd", knitTime(jTime(p[ENV_D], 12.0) * 1.5)], ["fs", p[ENV_S]],
    ["fr", knitTime(jTime(p[ENV_R], 12.0) * 1.5)]);
  const envOct = p[VCF_ENV] * 16383.0 / 128.0 / J_OCT * (envInv ? -1 : 1);
  if (Math.abs(envOct) > 6.0) notes.push(`filter env ${envOct >= 0 ? "+" : ""}${envOct.toFixed(1)} oct: knit's reaches 6`);
  const res = p[VCF_RES] / 127.0;
  out.push(["ftype", "ladder"], ["cut", knitCut(jCutHz(p[VCF_FREQ]))], ["res", clamp(127 * Math.min(1.0, 1.05 * res ** 1.5))],
    ["fenv", clamp(64 + envOct * 64.0 / 6.0)], ["ktrk", p[VCF_KYBD]]);
  const vibSemis = (p[DCO_LFO] / 127.0) ** 2 * 6.0;
  const vcfOct = p[VCF_LFO] * 8191.0 / 256.0 / J_OCT;
  const pwmLfo = pulse && !pwmManual && pwm > 0;
  const cand = [[vibSemis / 6.0, "vib"], [vcfOct / 3.6, "vcf"], [pwmLfo ? pwm / 127.0 : 0.0, "pwm"]];
  cand.sort((a, b) => (b[0] - a[0]) || (b[1] < a[1] ? -1 : b[1] > a[1] ? 1 : 0));   // (Python's reverse sort)
  const rate = clamp(8 + p[LFO_RATE] * 119.0 / 127.0, 8, 127);
  const delay = (p[LFO_DELAY] / 127.0) ** 2 * 3.0;
  const used = cand.filter((c) => c[0] > 0.02);
  if (used.length > 1) notes.push("LFO on " + used.slice(1).map((c) => c[1]).join(" and ") + " too: not made (knit's LFO has one target)");
  const kind = used.length ? used[0][1] : null;
  if (kind === "vib" && vibSemis <= 1.0) {
    out.push(["ltype", "wobble"], ["lspeed", rate], ["lamt", clamp(64 + vibSemis * 100.0 * 63.0 / 100.0)], ["ldest", 64],
      ["lx", delay > 0.01 ? clamp(64 - delay * 64.0 / 3.0) : 64]);
  } else if (kind) {
    const [dest, param, amt] = { vib: ["voice free", 1, 64 + vibSemis * 63.0 / 12.0],
      vcf: ["filter free", 0, 64 + (vcfOct * 127.0 / 8.64385619) * 63.0 / 127.0],
      pwm: ["synth free", 0, 64 + (pwm * 0.25) * 63.0 / 127.0] }[kind];
    out.push(["ltype", "wave"], ["lspeed", rate], ["lamt", clamp(amt)], ["ldest", dest], ["lx", param], ["lx2", "sine"]);
    if (delay > 0.05) notes.push(`LFO delay ${delay.toFixed(1)} s: not made (a wave LFO has no fade-in)`);
  } else out.push(["ltype", "off"]);
  out.push(["hpf", { 0: 0, 1: 0, 2: 69, 3: 99 }[hpf]], ["oct", OCT_NAME[1 + rng]], ["vmode", "poly"],
    ["volume", clamp(90 * 2.0 ** ((p[VCA_LEVEL] - 64) / 127.0))]);
  if (hpf === 0) notes.push("HPF 0's bass lift: not made");
  if (chorus) out.push(["itype", "juno"], ["i1", chorus - 1], ["i2", 70], ["s2mix", 80]);
  else out.push(["itype", "off"]);
  return { out, notes };
}

// ---- Prophet-5 (Rev 4): F0 01 32 02 group program, 133 bytes packed, F7 (prophet_import.py)
const P = {};
["FREQ_A", "FREQ_B", "FINE_B", "SAW_A", "PULSE_A", "SAW_B", "TRI_B", "PULSE_B", "PW_A", "PW_B", "SYNC", "LOW_B", "KEY_B",
  "GLIDE", "LEVEL_A", "LEVEL_B", "NOISE", "CUTOFF", "RES", "KEY_FILTER", "FILTER_REV", "LFO_RATE", "LFO_INITIAL",
  "LFO_SAW", "LFO_TRI", "LFO_PULSE", "WHEEL_MIX", "WHEEL_FREQ_A", "WHEEL_FREQ_B", "WHEEL_PW_A", "WHEEL_PW_B",
  "WHEEL_FILTER", "POLY_ENV", "POLY_B", "POLY_FREQ", "POLY_PW", "POLY_FILTER", "VINTAGE", "PRESS_FILTER", "PRESS_LFO",
  "ENV_FILTER", "VEL_FILTER", "VEL_AMP", "ATT_F", "ATT_A", "DEC_F", "DEC_A", "SUS_F", "SUS_A", "REL_F", "REL_A",
  "RELEASE_ON", "UNISON", "UNI_COUNT", "UNI_DETUNE"].forEach((n, i) => { P[n] = i; });
const P_NAME = 65, P_NAME_LEN = 20, P_BEND = 86;
export const prophetLabel = (k) => `${1 + Math.floor(k / 40)}${1 + Math.floor(k / 8) % 5}${1 + (k % 8)}`;
export function prophetName(p) {
  let s = "";
  for (let i = 0; i < P_NAME_LEN; i++) s += String.fromCharCode(p[P_NAME + i]);
  return s.trim();
}
// a bank's bytes -> Map(program -> 133 bytes)
export function readProphet(b) {
  const out = new Map();
  for (let i = 0; i + 159 <= b.length; i++) {
    if (b[i] !== 0xF0 || b[i + 1] !== 0x01 || b[i + 2] !== 0x32 || b[i + 3] !== 0x02) continue;
    const f = b.subarray(i, i + 159);
    if (f[158] !== 0xF7 || f[4] > 4 || f[5] >= 40 || ![...f.subarray(1, 158)].every((x) => x < 128)) continue;
    const pk = f.subarray(6, 158), raw = [];
    for (let g = 0; g < 152; g += 8) for (let j = 0; j < 7; j++) raw.push(pk[g + j + 1] | ((pk[g] >> j) & 1) << 7);
    out.set(f[4] * 40 + f[5], Uint8Array.from(raw));
    i += 158;
  }
  return out;
}
const semis = (v) => Math.max(-12, Math.min(36, (v >> 1) - 12));
const pulseWave = (pw) => clamp(64 + Math.abs(pw - 64) * 63.0 / 64.0);
const oscWave = (saw, pulse, pw) => (saw && pulse ? 32 : pulse ? pulseWave(pw) : 0);
const pTime = (v) => 6.9 * 10.0 ** (4.0 * v / 127.0) / 1000.0;
const pAttack = (v) => 10.0 ** (4.0 * v / 127.0) / 1000.0;
const lfoHz = (v) => Math.min(500.0, Math.max(0.022, Math.exp(-3.10 + 0.0539 * v)));
const knitRate = (hz) => clamp(8 + 119.0 * log2(Math.max(hz, 0.1) / 0.1) / log2(300.0), 8, 127);
const divmod = (a, b) => { const q = Math.floor(a / b); return [q, a - q * b]; };
export function convertProphet(p) {
  const notes = [], out = [["engine", "knit"]];
  const aSounds = (p[P.SAW_A] || p[P.PULSE_A]) && p[P.LEVEL_A] > 0;
  const bSounds = (p[P.SAW_B] || p[P.TRI_B] || p[P.PULSE_B]) && p[P.LEVEL_B] > 0;
  const sweepA = p[P.POLY_FREQ] && p[P.POLY_ENV] > 0;
  const fmA = p[P.POLY_FREQ] && p[P.POLY_B] > 0;
  let aIs2 = !!(p[P.SYNC] || sweepA);
  if (!aIs2 && !aSounds && bSounds && !fmA) aIs2 = true;
  if (p[P.LOW_B] || !p[P.KEY_B])
    notes.push("osc B " + (p[P.LOW_B] ? "in its low (LFO) range" : "off the keyboard") + ": played as a keyed oscillator");
  const [o1, o2] = aIs2 ? ["B", "A"] : ["A", "B"];
  const lev = { A: aSounds ? p[P.LEVEL_A] : 0, B: bSounds ? p[P.LEVEL_B] : 0 };
  const st = { A: semis(p[P.FREQ_A]), B: semis(p[P.FREQ_B]) };
  if (o1 === "A") out.push(["e1", oscWave(p[P.SAW_A], p[P.PULSE_A], p[P.PW_A])]);
  else {
    if (p[P.TRI_B] && !(p[P.SAW_B] || p[P.PULSE_B])) notes.push("osc B a triangle as osc 1: knit's osc 1 plays a saw");
    out.push(["e1", oscWave(p[P.SAW_B], p[P.PULSE_B], p[P.PW_B])]);
  }
  if (p[P.UNISON]) {
    const n = p[P.UNI_COUNT] > 0 && p[P.UNI_COUNT] <= 7 ? p[P.UNI_COUNT] : p[P.UNI_COUNT] === 0 ? 5 : 7;
    out.push(["e2", clamp(p[P.UNI_DETUNE] * 20)], ["e3", n], ["e4", 64], ["vmode", "mono"]);
  } else out.push(["e2", 0], ["e3", 1], ["e4", 0], ["vmode", "poly"]);
  const cents = (st[o2] - st[o1]) * 100.0 + p[P.FINE_B] * 0.75 * (o2 === "B" ? 1 : -1);
  const iv = pyRound(cents / 100.0);
  if (Math.abs(iv) > 24) notes.push(`osc ${o2} ${iv >= 0 ? "+" : ""}${iv} semitones from osc ${o1}: knit's osc 2 reaches 24`);
  out.push(["x1", clamp(64 + iv, 40, 88)], ["fine", clamp(64 + cents - 100.0 * iv, 14, 114)]);
  if (o2 === "A")
    out.push(...(p[P.SAW_A] && !p[P.PULSE_A] ? [["o2wave", "saw"]]
      : p[P.PULSE_A] && !p[P.SAW_A] ? [["o2wave", "pulse"], ["o2shape", pulseWave(p[P.PW_A])]] : [["o2wave", "saw"]]));
  else
    out.push(...(p[P.TRI_B] && !(p[P.SAW_B] || p[P.PULSE_B]) ? [["o2wave", "triangle"]]
      : p[P.PULSE_B] && !p[P.SAW_B] ? [["o2wave", "pulse"], ["o2shape", pulseWave(p[P.PW_B])]] : [["o2wave", "saw"]]));
  const top = Math.max(lev[o1], lev[o2], 1);
  out.push(["k1cut", clamp(127 * (1.0 - lev[o1] / top))], ["x2", clamp(127 * lev[o2] / top)], ["x3", 0],
    ["x4", clamp(p[P.NOISE] * 0.8 * 127 / top)], ["kanalog", p[P.VINTAGE]]);
  const [octs, rem] = divmod(st[o1], 12);
  if (rem) notes.push(`osc ${o1} ${rem} semitones above an octave: knit plays it on the octave`);
  out.push(["oct", OCT_NAME[clamp(2 + octs, 0, 4)]]);
  let sweep = sweepA ? pyRound(p[P.POLY_ENV] * 2.65 * 12 / 127) : 0;
  const cross = fmA && !aIs2 ? pyRound(127 * Math.sqrt(Math.min(1.0, p[P.POLY_B] / 127 * 1.1 / 6.0))) : 0;
  if (fmA && aIs2) notes.push("Poly-Mod osc B on A's frequency beside sync or the sweep: not made (knit's cross runs osc 2 into osc 1)");
  let swto = 0;
  if (p[P.POLY_PW] && p[P.POLY_ENV] && o1 === "A" && p[P.PULSE_A] && !sweep) { swto = 1; sweep = pyRound(p[P.POLY_ENV] * 48 / 127); }
  else if (p[P.POLY_PW] && (p[P.POLY_ENV] || p[P.POLY_B]))
    notes.push("Poly-Mod on A's pulse width: not made" + (!p[P.POLY_ENV] ? " (by osc B)" : ""));
  if (p[P.POLY_FILTER] && p[P.POLY_B]) notes.push("Poly-Mod osc B on the filter: not made");
  out.push(["ksync", p[P.SYNC] ? 1 : 0], ["ksweep", Math.min(sweep, 48)], ["kcross", cross], ["ksub", "sine"], ["kswto", swto]);
  const relA = p[P.RELEASE_ON] ? p[P.REL_A] : 0, relF = p[P.RELEASE_ON] ? p[P.REL_F] : 0;
  out.push(["a", knitTime(pAttack(p[P.ATT_A]))], ["d", knitTime(pTime(p[P.DEC_A]))], ["s", p[P.SUS_A]],
    ["r", knitTime(pTime(relA))], ["fa", knitTime(pAttack(p[P.ATT_F]))], ["fd", knitTime(pTime(p[P.DEC_F]))],
    ["fs", p[P.SUS_F]], ["fr", knitTime(pTime(relF))]);
  const kf = { 0: 0.0, 1: 0.5, 2: 1.0 }[p[P.KEY_FILTER]] ?? 1.0;
  const hz = 870.0 * 2.0 ** ((p[P.CUTOFF] - 67) / 15.2) * 2.0 ** (2.0 * kf);
  const envOct = p[P.ENV_FILTER] * 0.046 + (p[P.POLY_FILTER] ? p[P.POLY_ENV] / 127.0 * 8.0 : 0.0);
  if (envOct > 6.0) notes.push(`filter env +${envOct.toFixed(1)} oct: knit's reaches 6`);
  out.push(["ftype", "ladder"], ["cut", knitCut(hz)], ["res", clamp(127 * Math.min(1.0, 1.05 * (p[P.RES] / 127) ** 1.5))],
    ["fenv", clamp(64 + envOct * 64.0 / 6.0)], ["ktrk", clamp(127 * kf)]);
  if (!p[P.FILTER_REV]) notes.push("the Rev 1/2 (SSI) filter: played as the Rev 3 (Curtis)");
  const depth = p[P.LFO_INITIAL];
  const lc = p[P.WHEEL_FREQ_A] || p[P.WHEEL_FREQ_B] ? 0.052 * depth ** 1.74 : 0.0;
  const fOct = p[P.WHEEL_FILTER] ? depth / 127.0 * 4.0 : 0.0;
  const pw = p[P.WHEEL_PW_A] || p[P.WHEEL_PW_B] ? depth / 127.0 : 0.0;
  const cand = [[lc / 1200.0, "vib"], [fOct / 4.0, "vcf"], [pw, "pwm"]];
  cand.sort((a, b) => (b[0] - a[0]) || (b[1] < a[1] ? -1 : b[1] > a[1] ? 1 : 0));
  const used = cand.filter((c) => c[0] > 0.01);
  if (used.length > 1) notes.push("LFO on " + used.slice(1).map((c) => c[1]).join(" and ") + " too: not made (knit's LFO has one target)");
  const rate = knitRate(lfoHz(p[P.LFO_RATE]));
  if (lfoHz(p[P.LFO_RATE]) > 30.0 && used.length) notes.push(`LFO ${lfoHz(p[P.LFO_RATE]).toFixed(0)} Hz: knit's reaches 30`);
  const shape = p[P.WHEEL_MIX] >= 64 ? "random" : p[P.LFO_SAW] && !p[P.LFO_TRI] ? "saw"
    : p[P.LFO_PULSE] && !p[P.LFO_TRI] ? "square" : "sine";
  const kind = used.length ? used[0][1] : null;
  if (kind === "vib" && lc <= 100.0 && shape === "sine")
    out.push(["ltype", "wobble"], ["lspeed", rate], ["lamt", clamp(64 + lc * 63.0 / 100.0)], ["ldest", 64], ["lx", 64]);
  else if (kind) {
    const [dest, param, amt] = { vib: ["voice free", 1, 64 + lc / 100.0 * 63.0 / 12.0],
      vcf: ["filter free", 0, 64 + (fOct * 127.0 / 8.64385619) * 63.0 / 127.0], pwm: ["synth free", 0, 64 + pw * 32.0] }[kind];
    out.push(["ltype", "wave"], ["lspeed", rate], ["lamt", clamp(amt)], ["ldest", dest], ["lx", param], ["lx2", shape]);
  } else out.push(["ltype", "off"]);
  if (p[P.PRESS_FILTER] || p[P.PRESS_LFO]) notes.push("aftertouch routes: not made");
  const bend = p[P_BEND];
  out.push(["porta", p[P.GLIDE]], ["bend", bend === 0 ? "off" : bend <= 7 ? String(bend) : "octave"], ["hpf", 0],
    ["volume", 100], ["itype", "off"], ["ctype", "off"]);
  return { out, notes };
}

// ---- a preset's bytes (LOOM-8's user_preset_t) from a converter's parameters, on knit's defaults as LOOM-8 sets them
// (an engine change: sound_default), each value as the simulator's knob takes it (a list's name, or a number; osc 2's
// wave sets its shape where the wave sits, ui.c set_tp)
function valueOf(T, name, v) {
  const list = T.lists[name];
  if (list && typeof v === "string") {
    const k = list.findIndex((n) => n && n.toLowerCase() === v.toLowerCase());
    if (k >= 0) return k;
  }
  if (typeof v === "number") return v;
  if (/^-?\d+$/.test(v)) return +v;
  throw new Error(`no value ${v} for ${name}`);
}
export function buildPreset(T, name, out) {
  const par = T.defaults.slice();
  for (const [n, v] of out) {
    const p = T.params[n];
    if (p === undefined) throw new Error(`no parameter ${n}`);
    const x = valueOf(T, n, v);
    if (n === "engine") continue;                  // (knit's defaults are the start)
    par[p] = x & 255;
    if (n === "o2wave" && x >= 1 && x <= 3) par[T.params.o2shape] = [0, 64, 127][x - 1];   // (saw, square, pulse)
  }
  const raw = new Uint8Array(T.preset_len);
  for (let i = 0; i < T.name_len - 1 && i < name.length; i++) raw[i] = name.charCodeAt(i) & 127;
  raw[T.name_len] = T.knit;
  for (let i = 0; i < T.ntp_sound; i++) raw[T.name_len + 1 + i] = par[i];
  T.ps_x.forEach((p, k) => { raw[T.name_len + 1 + T.ntp_sound + k] = par[p]; });
  return raw;
}
// a preset's volume (its byte) and how the level fit sets it (loom_presets.py level_fit: a two-octave chord's peak at
// -28 dBFS, measured 12 dB down, the volume a square law): the page measures with the emulator (web.c loom_x_chord_peak)
export const LEVEL_DB = -28.0;
export const volIndex = (T) => T.name_len + 1 + T.params.volume;
export function fitVolume(vol, low, peakDb) {
  const pk = peakDb + 40.0 * Math.log10(vol / low);
  return { pk, vol: Math.max(1, Math.min(127, pyRound(vol * 10.0 ** ((LEVEL_DB - pk) / 40.0)))) };
}

// ---- the level fit in the emulator (app.wasm: the site's ../emu/app.wasm)
// boot it as the export page does (render.js): its clock ours, the master at the simulator's 2048 (the tools' level)
export async function bootEmu(wasm) {
  let mem;
  const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
    fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } },
    { get: (t, k) => (k in t ? t[k] : () => 0) });
  const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
  const ex = instance.exports;
  mem = ex.memory;
  if (ex._initialize) ex._initialize();
  ex.web_master(2048);
  ex.web_boot();
  if (!ex.loom_x_chord_peak) throw new Error("This site's emulator is older than its presets page.");
  return { ex, mem };
}
// a preset's raw bytes with its volume set so a two-octave chord peaks at LEVEL_DB (loom_presets.py level_fit): the
// chord measured at half the volume (12 dB down: clear of the output's ceiling) -> { raw, pk, vol }
export function levelFit(emu, T, raw) {
  const vi = volIndex(T), vol = raw[vi] || 100, low = Math.max(1, pyRound(vol / 2));
  const probe = raw.slice();
  probe[vi] = low;
  new Uint8Array(emu.mem.buffer, emu.ex.loom_x_pbuf(), probe.length).set(probe);
  const peak = emu.ex.loom_x_chord_peak(T.fmt);
  if (peak < 0) throw new Error("the emulator refused the preset");
  const f = fitVolume(vol, low, 20.0 * Math.log10(peak / 1e6 + 1e-9));
  const out = raw.slice();
  out[vi] = f.vol;
  return { raw: out, pk: f.pk, vol: f.vol };
}
