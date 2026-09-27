# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2715.25-ocr-native-size` (base `d456ffc`).

### OCR coordinates (verified path)
1. Store regions in **page space only**.
2. Crop/orient/grade applied only in `regionImageRect` at paint.
3. Appearance OCR: scale to `cachedSize` → materializeDisplay → OCR → remap to page.
4. Full-page URI OCR: no session crop in thumtoo args.
5. Docs: `docs/OCR_COORDINATES.md`.

### Apply
```bash
git pull --ff-only …/biltoo-2715.25-ocr-native-size-d456ffc.bundle HEAD
```

### Limits
- Free-rotated crop: AABB of corners (no rotated region quads).
