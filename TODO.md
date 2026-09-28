# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2740.2-text-layer-leave-doubleview` (base `636e70e`).

### 2740.2
- Text: `layerForItem` no longer falls back to primary layer for every underlay
  (spread pages painted their own OCR boxes only)
- Leaving Double View clears multi underlays + reloads single page

### Apply
```bash
git pull --ff-only …/biltoo-2740.2-text-layer-leave-doubleview-636e70e.bundle HEAD
```
