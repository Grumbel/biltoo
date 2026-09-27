# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.24-ocr-page-space` (base `d456ffc`).

### OCR coordinates
- Regions **always page space**; crop/orient/grade are view transforms at paint.
- Appearance OCR: materializeDisplay → OCR → remap to page (not store display coords).
- No session crop in thumtoo ensure/run OCR args.
- Docs: `docs/OCR_COORDINATES.md`.

### Apply
```bash
git pull --ff-only …/biltoo-2715.24-ocr-page-space-d456ffc.bundle HEAD
```

### Known limit
- Free-rotated crop: AABB only (no rotated region quads yet).
