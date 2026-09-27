# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.11-text-rotate-want` (base `d456ffc`).

### Later
- Tesseract OSD / per-line baseline for rotated overlay drawing (see prior tip notes).

### Fixes
- Text boxes on **rotated** pages: map via `wantAppearanceForItem` (contentBake
  turns + applied), not session-or-applied alone.
- Dense RGB888 pack for graded appearance OCR.
- Requires thumtoo **345.2**.

### Apply
```bash
git pull --ff-only …/biltoo-2716.11-text-rotate-want-d456ffc.bundle HEAD
```
