# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2837.1-mark-text-selection (on 2836.1 stack).

### 2835.1
- Slideshow pure-phase: `tickPrimaryTileLod` drives phase tile sessions via
  `tickSlideshowTileLod` (ImageItems are not tileLodWanted while hidden).
- Re-arm LOD timer + viewport update until phase coverage settles — higher
  tiles were loading (activity) but dwell stayed on coarse parents when motion
  off / paint infrequent.
- Phase buffer orient uses `PixelKind::FullSource` (quality climb intent).
- Cover paint gets session `ContentXform` for orient parity with Gallery.


### 2836.1 — documentation hygiene
- **AGENTS.md:** tip points at TODO (no hard-coded NNN); doc roles table; SESSION is archived handoff.
- **Merged / stubs:** GALLERY_SOFT → KILL_SOFT + GALLERY_PIXELS; IMAGECACHE_PUT_AUDIT → PIXEL_HOST_CACHE; RESEARCH_TILE_OVERLAP retired note.
- **Archived banners:** AUDIT, SESSION, TILE_DRAW_INVESTIGATION, INVESTIGATION_MODE_EMPTY, ECS_GUI_BYPASSES, PIXEL_PIPELINE_REDESIGN.
- **ACTIVITY.md:** documents partial PerformancePanel / activity_snapshot reality vs design target.
- **TILE_LOD.md:** slideshow pure-phase LOD + cover orient; SLIDESHOW cross-link.
- **RELEASE_0.2.0.md:** VERSION claim no longer asserts tree is still 0.2.0-dev.
- **Cross-links:** PreferCache/soft policy links retarget KILL_SOFT where they pointed at GALLERY_SOFT.

### 2837.1 — Mark selection (tool unification)
- Annotations panel: **Mark selection** → `AnnotationController::markTextSelection`
  (TextSelection / multi bag → HighlightQuad, panel colour).
- Text Highlighter tool kept; demotion still optional (ANNOTATION_OVERLAY §14).
- Panel button enabled when `TextLayerSession::hasSelection()`.

### Required thumtoo
thumtoo-010.2-tile-supersede-activity-tests (includes 010.1)

### Prior
2834.2 cover orient docs/tests; 2834.1 cover orient paint
