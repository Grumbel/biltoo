# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.4-dpi-lambda-capture` (base `d456ffc`).

### Fixes
1. Text overlays: `ContentXform::mapSourceRectToDisplay` + scale into item layout.
2. OCR panel Source DPI (0=Auto); document OCR worker captures `dpiCopy`.
3. Requires thumtoo **345.2**.

### Apply
```bash
git pull --ff-only …/biltoo-2716.4-dpi-lambda-capture-d456ffc.bundle HEAD
```
