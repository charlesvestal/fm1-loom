// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "backup" (fm1.toml emu = ["backup"], an app with backup = true), the worklet's side: the saved objects
// as a backup in the file format of web/backup/index.html and tools/fm1_backup.py (device/backup.c's image, made and
// checked by the same code as the host simulator's `backup` and `restore`). The page's side is backup.js.
//   { cmd: "make" }              -> { backup: ArrayBuffer, playing }   the image (a backup while playing leaves out what
//                                    the app has not saved: flag bit 0)
//   { cmd: "restore", image }    -> { restored: 0 | rc }   the image checked whole, then every object replaced and the app
//                                    reloaded (the FM-1's restore once SEL answers yes); rc 10 playing, 3 not this app's
//                                    or version's, 4 6 damaged

fm1Part({
  name: "backup",
  message(fm1, m) {
    const ex = fm1.ex;
    if (!ex.web_backup_make) {                       // built without FM1B_BACKUP: the app has no backup
      fm1.part("backup", { none: 1 });
      return;
    }
    if (m.cmd === "make") {
      const n = ex.web_backup_make();
      const image = new Uint8Array(fm1.mem.buffer, ex.web_backup_buf(), n).slice();
      fm1.part("backup", { backup: image.buffer, playing: (image[8] & 1) === 1 }, [image.buffer]);
    } else if (m.cmd === "restore") {
      const image = new Uint8Array(m.image);
      let rc = 3;
      if (image.length <= ex.web_backup_max()) {
        new Uint8Array(fm1.mem.buffer, ex.web_backup_buf(), image.length).set(image);
        rc = ex.web_backup_restore(image.length);
      }
      fm1.part("backup", { restored: rc });
    }
  },
});
