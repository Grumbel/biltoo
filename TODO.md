# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2712.5-multipage-text-selection` (base `b65f69e`).

### 2712.5 — Multi-page text selection bag
- `TextSelection` / `TextSelRef` (`SessionImageId` + region index + text snapshot).
- Current page still uses `selectedRegions` for paint; bag keeps other pages.
- Page change restores current-page projection; copy uses joined snapshots.
- Click-miss clears only the current page’s refs in the bag.

### 2712.4 — Click-select text region
### 2712.3 — Tools strip + Crop
### 2712.2 — Tool-aware pan + Select→text
### 2712.1 — Shared ViewInteraction

### Next
- Ctrl-add / Shift-range region click modifiers
- Panel UI showing multi-page selection spans

### Apply
```bash
git pull --rebase …/biltoo-2712.5-multipage-text-selection-b65f69e.bundle HEAD
```
