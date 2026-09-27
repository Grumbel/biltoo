# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.6-text-applied-xform` (base `d456ffc`).

### Fixes
- Text overlays map via **itemAppliedContentXform** so crop draft (full-page
  orient-only) and applied crop use the same transform as the painted sample.
- OCR Run always re-runs; Source DPI in panel.
- Requires thumtoo **345.2**.

### Apply
```bash
git pull --ff-only …/biltoo-2716.6-text-applied-xform-d456ffc.bundle HEAD
```
