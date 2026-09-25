# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2649.1-crop-panel` (base `9740316`).

### Done
- Orient multi-apply + undo macro (2646).
- Colour multi-apply (2647): Apply to selection + undo bag.
- Reset Content Appearance GUI-thread fix (2648).
- **Crop panel v1 (2649):** View → Show Crop Panel dock.
  - Modes: Manual margins (L/T/R/B px) · Autocrop (threshold).
  - Extra margin expand after mode result.
  - Apply to current / Apply to selection; Reset crop current / selection.
  - `CropController::applyCropRecipeToTargets` / `ToItems` + reset counterparts;
    `CropRecipeUtil::computeCropRect`; undo macro when N>1.
  - GUI-safe: autocrop uses item/ImageCache samples only (no sync loadThumbnail).

### Next
1. Crop panel polish: live preview on current only; normalised 0–1 fields;
   skip when logical size unknown (status message).
2. Template-from-page / even-odd recipes / stack preview (later).
3. Expand targets beyond live items.

### Apply
```bash
git pull --ff-only …/biltoo-2649.1-crop-panel-9740316.bundle HEAD
```
