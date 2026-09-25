# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2648.1-reset-appearance-gui-thread` (base `9740316`).

### Done
- Orient multi-apply + undo macro (2646).
- **Colour multi-apply:** Adjustments panel **Apply to selection** →
  `applyColorAdjustmentsToTargets` with undo macro. Sliders still edit current
  target only. Button enabled when `transformTargets().size() > 1`.
- **Fix Reset Content Appearance GUI assert:**
  `reinstallModePixelsAfterIdentityReset` no longer calls
  `ImageLoader::loadThumbnail` on the GUI thread (Gallery used cache-only +
  `scheduleGalleryDecode`; Image/Workspace schedule PathRaster climb on cache
  miss). Same for cold-cache branch of `rematerializeItemContent`
  (`ImageCache::get` / `ensure` instead of sync loadThumbnail).

### Next
1. Crop panel (manual / autocrop+threshold / margins / reset).
2. Expand targets beyond live items; template/stack later.

### Apply
```bash
git pull --ff-only …/biltoo-2648.1-reset-appearance-gui-thread-9740316.bundle HEAD
```
