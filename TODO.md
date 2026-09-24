# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2608.1-restore-reorder-peel-rebind** (base `7d823d8`).

### This tip
- **Bugfix:** `ImageView::reorderItemsByPaths` was removed in the transform
  peel but remains a **DisplayPipelineHost** pure virtual + public override.
  Restored thin forward to `m_workspace.reorderItemsByPaths`.
- **Peel:** `rebindWorkspaceSession` → `hostWorkspace().rebindSession`
  (declaration + callers; was another declared-without-body orphan).

### Wave C status (mode shell)
`setViewMode` / `setLayoutMode` / `reloadFromDisk` / `hardReloadFromDisk` are
already **thin dispatchers** (leave prep + controller enter/leave). Keep them
on ImageView as the mode shell; do not push mode branching into MainWindow.

### Prior (2604–2607)
~65+ pure-forward methods peeled; SelectionGeometry include; orphan rotate /
setWorkspaceDefaultViewScale fixed.

### Still on ImageView
- Mode shell dispatchers (above)
- statusText / hudFileName / loadingStatusHudLine / refreshStatus
- DisplayPipelineHost surface + QGraphicsView overrides
- Shell queries (itemPaths, selectedPaths, imageSize, pendingDecodeCount, …)

### Apply
```bash
git pull --ff-only …/biltoo-2608.1-restore-reorder-peel-rebind-7d823d8.bundle HEAD
```
