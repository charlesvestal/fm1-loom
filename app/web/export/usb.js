// SPDX-License-Identifier: MPL-2.0
// The export page's "Read from the FM-1": the backup taken straight from LOOM-8 over USB, as the backup page takes it,
// and handed to the export as the bytes of a backup file (nothing written to the FM-1: the backup's BU_REQ and the
// samples' INFO and READ only). tools/make_pages.py inlines it into the page inside one function, after what it uses:
// fm1-base's web/fm1ports.js and app/web/samples/loom_samples.js (seekLink, notFound: LOOM-8 found as the samples
// page finds it), the backup page's protocol (PB, from base/web/backup/index.html as the backup page has it filled in),
// its EXTRAS (app/web/backup/extras.js: the sample bank) and its Link (as QLink). tests/host/export_web.mjs runs it
// against a simulated FM-1.

// midi: a MIDIAccess with SysEx; on.status(text), on.progress(fraction, or null while the size is not known) ->
// { file: the backup file's bytes (the backup, then the samples' section, as the backup page saves it), flags (bit 0:
// LOOM-8 was playing, so what it hadn't saved yet is left out) }; throws in plain words
async function readFM1(midi, on = {}) {
  const status = on.status || (() => {}), progress = on.progress || (() => {});
  const again = (e) => new Error(String(e.message || e).replace(/Press Back up again/g, "Press Read from the FM-1 again"));
  status("Looking for LOOM-8…");
  const res = await seekLink(midi);
  if (!res.link) throw new Error(notFound(res, "the export page").replace(/connect again/, "press Read from the FM-1 again"));
  const { input, output } = res.link;
  res.link.close();
  const link = new QLink(input, output);
  try {
    status("Reading the projects and presets from LOOM-8…");
    progress(null);
    let got;
    try { got = await PB.backup(link, () => progress(null)); } catch (e) { throw again(e); }
    const sections = [];
    for (const ex of EXTRAS) {
      status(`Reading ${ex.what} from LOOM-8. This can take up to a minute.`);
      progress(0);
      try { sections.push({ tag: ex.tag, data: await ex.backup(link, (f) => progress(f)) }); } catch (e) { throw again(e); }
    }
    return { file: PB.join(got.image, sections), flags: got.flags };
  } finally {
    link.close();
  }
}
