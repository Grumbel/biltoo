# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2604.1-peel-workspace-gallery-image-forwards** (base `7d823d8`).

### Wave A peel (this tip)
Removed **23 pure-forward public methods** from `ImageView`. Callers use
`hostWorkspace()` / `hostGallery()` / `hostImage()` instead.

| Was `ImageView::` | Now |
|-------------------|-----|
| clearWorkspace | hostWorkspace().clearWorkspace |
| hasWorkspaceContent | hostWorkspace().hasContent |
| hasTransformTargets / hasSingleCropTarget | hostWorkspace().… |
| selectedSessionIds / Indices | hostWorkspace().… |
| workspacePathOccurrenceCount | hostWorkspace().pathOccurrenceCount |
| captureSelectedWorkspaceClipboard | hostWorkspace().captureSelectedClipboard |
| placeWorkspaceClipboardItems | hostWorkspace().placeClipboardItems |
| removeWorkspaceSessionId / removeCanvasSessionIds / placeSessionIdsOnCanvas | hostWorkspace().… |
| focusGalleryItem / focusSessionId / focusSessionPath | hostGallery().focusItem / focusSessionId / focusSessionPath |
| revealGalleryPath / revealGallerySessionId | hostGallery().revealPath / revealSessionId |
| setTargetColorAdjustments / targetHasContentAppearance | hostImage().… |
| setGalleryReturnAvailable / setImageModeNavigationEnabled | hostImage().… |
| stopDeferredPacking | hostGallery().stopLayoutDebounceTimer (or self on GalleryController) |
| clearInteractionState | WorkspaceController::clearInteractionState (self) |

Header ~734 → ~660 lines. Routers lost the matching thin definitions.

### Still on ImageView (next)
**Do not peel** (DisplayPipelineHost overrides): prepareImageModeCanvas,
applyState, setItemSessionId, syncLive*, clearLiveContentMeta, …

**Wave A remainder (pure forwards):** setCentreProgress / clearCentreProgress
(shell), setContentEditMarksVisible, page-guide setters, more workspace
private routers if any public surface left.

**Wave B:** HUD / statusText / appearance gather.

**Wave C:** setViewMode / setLayoutMode / reloadFromDisk (orchestration —
thin dispatcher only; move branch bodies into mode controllers).

### Apply
```bash
git pull --ff-only …/biltoo-2604.1-peel-workspace-gallery-image-forwards-7d823d8.bundle HEAD
```
