// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "backup" (fm1.toml emu = ["backup"], an app with backup = true): the page's side. "Save backup" writes the
// emulated FM-1's saved objects to a file in the format web/backup/index.html and tools/fm1_backup.py use (the app's
// magic, version and objects, CRC-32 on every part and the whole), named as that page names it, so a backup taken here
// restores on an FM-1 with that page or tool, and one taken from an FM-1 loads here. "Load backup…" checks the file
// (the app's, this version, whole), asks, and replaces every object: what the app does after a restore on the FM-1.
// A file may carry the firmware's own sections after the backup's CRC (the page's EXTRAS); they are left alone.
// The worklet's side is backup_worklet.js.

const HEAD = 16, REC = 8;
const u16 = (b, o) => b[o] | b[o + 1] << 8;

/** the length of the backup itself in a file (its own sections, if any, come after): a file cut short throws */
export function imageLength(file) {
  if (file.length < HEAD + 4) throw new Error("This file is too short to be a backup.");
  const n = u16(file, 6);
  let pos = HEAD;
  for (let k = 0; k < n; k++) {
    if (pos + REC > file.length) throw new Error("This backup is cut short.");
    pos += REC + u16(file, pos + 2);
  }
  if (pos + 4 > file.length) throw new Error("This backup is cut short.");
  return pos + 4;
}

/** the file name the backup page gives: NAME-backup-YYYY-MM-DD.EXT */
export function fileName(name, ext, d = new Date()) {
  const pad = (v) => String(v).padStart(2, "0");
  return `${name.toLowerCase().replace(/[^a-z0-9]+/g, "-")}-backup-${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}.${ext}`;
}

/** what the app's answer to a restore says */
export function restoreText(rc, name) {
  if (rc === 0) return "Restored. The saved data is the backup's now.";
  if (rc === 10) return `${name} is playing: stop it first, then load the backup.`;
  if (rc === 3) return `That file is not a ${name} backup of this version. Nothing was changed.`;
  return "That backup is damaged. Nothing was changed.";
}

let post = () => {}, say = () => {}, info = {};

export function attach(ctx) {
  post = ctx.post;
  say = ctx.status || (() => {});
  info = ctx;
  if (!ctx.bar) return;
  const save = document.createElement("button"), load = document.createElement("button"), pick = document.createElement("input");
  save.type = load.type = "button";
  save.textContent = "Save backup";
  save.title = "The saved data to a backup file, as the backup page makes it";
  load.textContent = "Load backup…";
  load.title = "Put a backup back (from this emulator or an FM-1)";
  pick.type = "file";
  pick.hidden = true;
  save.addEventListener("click", () => post({ cmd: "make" }));
  load.addEventListener("click", () => pick.click());
  pick.addEventListener("change", async () => {
    const f = pick.files[0];
    pick.value = "";
    if (!f) return;
    try {
      const file = new Uint8Array(await f.arrayBuffer());
      const image = file.slice(0, imageLength(file));
      if (!window.confirm(`Replace everything ${info.name} saved with the backup in ${f.name}?`)) return;
      post({ cmd: "restore", image: image.buffer }, [image.buffer]);
    } catch (e) {
      say(`${f.name}: ${e.message}`);
    }
  });
  const anchor = ctx.bar.querySelector("#reset");
  ctx.bar.insertBefore(save, anchor);
  ctx.bar.insertBefore(load, anchor);
  ctx.bar.appendChild(pick);
}

export function message(m) {
  if (m.none) say(`${info.name} was built without backup (fm1.toml backup = true).`);
  else if (m.backup) {
    const a = document.createElement("a"), name = fileName(info.name, info.backupExt || "bak");
    a.href = URL.createObjectURL(new Blob([m.backup], { type: "application/octet-stream" }));
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    say(`Saved ${name}.` + (m.playing ? ` ${info.name} was playing, so what it had not saved yet is not in it.` : ""));
  } else if (m.restored !== undefined) {
    say(restoreText(m.restored, info.name));
  }
}
