# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2618.1-drop-unused-imagesize** (base `7d823d8`).

### This tip
Removed unused `ImageView::imageSize()` (no external callers; items expose
`ImageItem::imageSize()`). Header ~517 lines.

### Remaining non-host public (intentional shell)
- `selectedPaths`, `pendingDecodeCount`
- `statusText`, `hudFileName`, `loadingStatusHudLine`
- Mode shell: `setViewMode`, `setLayoutMode`, `reloadFromDisk`, `hardReloadFromDisk`
- `currentPath` — host-surface declaration (crop display)

### Apply
```bash
git pull --ff-only …/biltoo-2618.1-drop-unused-imagesize-7d823d8.bundle HEAD
```
