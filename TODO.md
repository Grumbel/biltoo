# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2719.1-scripting-brainstorm` (base `ec60473`).

### 2719.1 — Scripting API brainstorm (docs only)
- [docs/SCRIPTING.md](docs/SCRIPTING.md): hypothetical world-first scripting surface
  (handles, command table, hooks). Language skin Lua-or-Squirrel; no code.
- Cross-links: DOMAIN (world vs viewpoint), SCENE_LANGUAGE, AGENTS.

### Stack note
Prior tips 2717.1 (run-gdb quit) + 2718.2 (world vs viewpoint) are already on
`origin/master` through `ec60473`.

### Open product direction (not scheduled)
- TTS continues across mode switches / page flips; speak plan may advance
  across session images; Gallery highlights speech-cursor page.
- Surfaces report background tile/soft/OCR activity without owning it.
- Longer term: multiple documents in one process under the same rule.
- Scripting remains napkin-level until someone chooses to embed an interpreter.

### Later — crop-based packs (careful)
- **Re-enable Grid Crop** in UI: square cells, cover-scale + centre crop for *layout
  display only* — must **not** touch session/user content crop or durable appearance.
- Do not invent more “Fill” modes until Contact sheet + Strip rows feel right in use.

### OCR / rotation (later)
- Tesseract OSD / per-line baseline for rotated overlays.

### Apply
```bash
git pull --ff-only …/biltoo-2719.1-scripting-brainstorm-ec60473.bundle HEAD
```
