# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2605.1-peel-shell-workspace-gallery-forwards** (base `7d823d8`).

### Wave A continued (this tip)
Added `hostShell()` (`ViewShellChrome`). Peeled more pure-forward public
`ImageView` methods onto controllers / shell.

| Was `ImageView::` | Now |
|-------------------|-----|
| setCentreProgress / clearCentreProgress | hostShell().… |
| setBackground* / setWorkspaceBackground* / clearWorkspaceBackground / setViewBackground / setCheckerboard* / setContentEditMarksVisible | hostShell().… |
| raise/lower/opacity/resetItem* / select* / duplicateSelected / applyToolDragMode | hostWorkspace().… |
| enterGallery | hostGallery().enterGallery |
| copySessionAppearance / flushColorAdjustCommit / clearSceneKeepingStashes | hostImage().… (self-call on ImageController for clearScene) |
| applyCropAppearance | hostCrop().applyCropAppearance |

Header ~660 → ~600 lines.

### Prior peel (2604.1)
23 workspace/gallery/image forwards — see previous tip.

### Still on ImageView (next)
**Keep:** DisplayPipelineHost overrides, QGraphicsView overrides, mode
orchestration (`setViewMode` / `setLayoutMode` / `reloadFromDisk`),
signals, host surface.

**Wave B:** HUD / statusText / hudFileName / loadingStatusHudLine /
appearance gather that still routes through the view.

**Wave C:** thin `setViewMode` dispatcher; push branch bodies into mode
controllers.

### Apply
```bash
git pull --ff-only …/biltoo-2605.1-peel-shell-workspace-gallery-forwards-7d823d8.bundle HEAD
```
