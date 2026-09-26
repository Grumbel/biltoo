# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2712.4-text-click-select` (base `b65f69e`).

### 2712.4 — Click-select text region
- Tiny rubber (click) selects the tightest region under the pointer.
- Miss clears selection (via clear before hit).

### 2712.3 — Tools strip + Crop
### 2712.2 — Tool-aware pan + Select→text rubber
### 2712.1 — Shared ViewInteraction

### Next
- Multi-page TextSelection (`SessionImageId` + region index)
- Ctrl/Shift multi-region click modifiers

### Apply
```bash
git pull --rebase …/biltoo-2712.4-text-click-select-b65f69e.bundle HEAD
```
