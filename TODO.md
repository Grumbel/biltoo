# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2776.1-annot-page-blend` (base `b8a0cf3`).

### 2776.1
- Text highlighter uses **layerForItem(primary)** / path cache (not stale other-page session layer)
- regionImageRectFor for hit-test
- Multiply: software raster; **GL viewport** → translucent SourceOver (~42% opacity) so glyphs stay readable

### Apply
```bash
git pull --ff-only …/biltoo-2776.1-annot-page-blend-b8a0cf3.bundle HEAD
```
