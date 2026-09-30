# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2839.2-gallery-status-missing-source (on 2839.1 stack).

### Stopping point (2838 feature line complete)
- Document live negative tile scales for PDF/DjVu/EPUB page refs.
- SVG: no negative scales (not a document-page URI path in thumtoo).
- Open/probe errors: library strings only; File not found; empty canvas.
- Source unavailable memo + ImageItem banner + filmstrip ! badge; clear on sizeReady.


### Stopping point
Tool unification lean path for text markup is complete: Exclusive radio,
Mark selection (panel + Ctrl+Shift+M), Text Highlighter kept. Next work is
optional demotion of Text Highlighter after field use, or Workspace free
rotation vs content orient — not blocking.


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

### 2837.2
- Mark selection shortcut **Ctrl+Shift+M** + Image menu action (same path as panel).

### 2837.3
- `SlideshowController::tickSlideshowTileLod` is **public** (called from
  `DisplayPipelineController::tickPrimaryTileLod`). Fix private-access build error.

### 2837.4
- Parentheses around `&&`/`||` in `paintSlideshowTiles` (Wparentheses).

### 2838.1 — Document live tiles + open errors
- Negative tile scales for PDF/DjVu/EPUB page refs (`kDocumentLiveMinScale` −4);
  lod_math + tests; Image/Slideshow floors; SVG still raster path (doc note).
- Open errors: `formatLoadErrorMessage` classifies library “not found”; empty
  canvas + HUD + `reportSessionOpenFailed` set `lastLoadError` (no exists()).

### 2838.2
- Source-unavailable process memo (`noteSourceUnavailable` / clear on sizeReady).
- ImageItem banner: “Source missing — cached preview only” when memo set.
- formatLoadErrorMessage notes unavailable on not-found classification.

### 2838.3
- Size probe miss (`!reply.size`) notes source unavailable.
- Filmstrip cell badge when source unavailable with cached preview.

### 2839.1
- QToolBars `setMovable(true)` (main, Tools, location, search). Floatable still off.

### 2839.2
- Gallery empty status uses session path count (not “no images” when filmstrip has rows).
- Source-missing ImageItem banner + filmstrip badge ~2× size.

### Required thumtoo
thumtoo-010.2-tile-supersede-activity-tests (includes 010.1)

### Prior
2834.2 cover orient docs/tests; 2834.1 cover orient paint
