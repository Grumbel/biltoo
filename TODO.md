# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2732.1-spread-p2-cross-page-text` (base `636e70e`).

### Spread P0–P1 complete; P2 started

#### 2732.1 — cross-page text (P2 core)
- `ensureMemberLayers` / `layerForItem` / `regionImageRectFor`
- Rubber-band classifies hits per underlay; `TextSelection` bag by sid
- Paint region outlines + selection on every spread member
- Glyphs/search/hover/TTS remain primary-only this slice
- Removed N>1 text paint/select gates from P1 deferral

### Still open
- Text panel rows flattened across members
- Search hits on secondary pages
- P3 TTS spans across members

### Apply
```bash
git pull --ff-only …/biltoo-2732.1-spread-p2-cross-page-text-636e70e.bundle HEAD
```
