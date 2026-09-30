# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2834.2-cover-orient-docs-tests (on 2834.1 stack).

### 2834.2
- Docs: GALLERY_PIXELS + TILE_LOD — cover shares orient path with ImageItem
- Test: `ContentOrientPaintContractTest::coverDestDensity_usesLayoutSize_whenOriented`
  (dest aspect = layoutSize; density must not use native on odd turns)

### 2834.1
- `CoverPaintArgs::xform` → `paint_tiles_display` when orient/crop/flip
- Gallery virtual + filmstrip pass session appearance

### Required thumtoo
thumtoo-010.2-tile-supersede-activity-tests (includes 010.1 activity finish)

### Still open
- Text Highlighter → Mark selection (ANNOTATION_OVERLAY §14; design only)
