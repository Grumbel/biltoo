# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2719.2-scripting-brainstorm` (base `ec60473`).

### 2719.2 — Scripting API brainstorm (docs polish)
- [docs/SCRIPTING.md](docs/SCRIPTING.md): second pass — layer cake (transport /
  skin / command table / world), handles vs values, explicit targets vs ambient
  current, anti-patterns, phased path with JSON/table-first option.
- Still hypothetical; no runtime code.

### Stack
- `ec60473` origin/master (includes 2717.1 gdb quit + 2718.2 world vs viewpoint)
- 2719.1 first scripting draft → 2719.2 polish (this tip; full stack in one bundle)

### Open product direction (not scheduled)
- TTS across modes / pages; Gallery speech-cursor highlight.
- Activity reporting as world state.
- Scripting remains napkin-level until a command-table inventory is worth doing.

### Later — crop-based packs (careful)
- Re-enable Grid Crop as layout-only clip; do not invent more Fill modes early.

### OCR / rotation (later)
- Tesseract OSD / per-line baseline for rotated overlays.

### Apply
```bash
git pull --ff-only …/biltoo-2719.2-scripting-brainstorm-ec60473.bundle HEAD
```
