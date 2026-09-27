# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2716.1-page-y-up` (base `d456ffc`).

### OCR coordinates
1. Store regions in **page space only** (crop/orient/grade are paint-time).
2. Honour `PageTextLayer::pageYUp` (thumtoo TTL7); fallback `pageSpaceYUpForPath`.
3. Appearance OCR: `cachedSize` → materializeDisplay → OCR → remap to page.
4. Full-page URI OCR: no session crop in thumtoo args.
5. Docs: [docs/OCR_COORDINATES.md](docs/OCR_COORDINATES.md).

### Requires
thumtoo tip **345.1-page-y-up** (or newer) for document OCR layers with Y-up.

### Apply
```bash
git pull --ff-only …/biltoo-2716.1-page-y-up-d456ffc.bundle HEAD
```

### Limits
- Free-rotated crop: AABB of corners (no rotated region quads).
