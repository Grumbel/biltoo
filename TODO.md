# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2607.1-fix-orphan-api-peel-pad-marks** (base `7d823d8`).

### This tip
- **Bugfix:** `rotateContentByQuarterTurns` / `setWorkspaceDefaultViewScale`
  were declared on `ImageView` without definitions after earlier peels.
  Callers now use `hostImage()`.
- **Peel:** `slideshowPadColor` → `hostSlideshow().padColorForPaint()`;
  `contentEditMarksVisible` → `ImageItem::contentEditMarksVisible()`.
- WorkspaceController uses `updateSceneRect()` / `hostImage().setWorkspaceDefaultViewScale()`
  instead of ImageView wrappers.
- Header cleanup: removed orphan sticky/zoom/duplicate docs + leftover
  `duplicateSelected` declaration.

Header ~573 → ~526 lines.

### Cumulative (2604–2607)
~65+ pure-forward public methods removed; a few link-break orphans closed.

### Still on ImageView (next)
- **Complex gather:** statusText, hudFileName, loadingStatusHudLine, refreshStatus
- **Mode shell:** setViewMode / setLayoutMode / reloadFromDisk / hardReloadFromDisk
  (orchestration — push branch bodies into controllers; keep thin dispatcher)
- **Host surface:** DisplayPipelineHost overrides, queries (itemPaths, imageSize, …)
- **QGraphicsView overrides**

### Apply
```bash
git pull --ff-only …/biltoo-2607.1-fix-orphan-api-peel-pad-marks-7d823d8.bundle HEAD
```
