# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2835.1-slideshow-tile-lod-climb (on 2834.2 stack).

### 2835.1
- Slideshow pure-phase: `tickPrimaryTileLod` drives phase tile sessions via
  `tickSlideshowTileLod` (ImageItems are not tileLodWanted while hidden).
- Re-arm LOD timer + viewport update until phase coverage settles — higher
  tiles were loading (activity) but dwell stayed on coarse parents when motion
  off / paint infrequent.
- Phase buffer orient uses `PixelKind::FullSource` (quality climb intent).
- Cover paint gets session `ContentXform` for orient parity with Gallery.

### Required thumtoo
thumtoo-010.2-tile-supersede-activity-tests (includes 010.1)

### Prior
2834.2 cover orient docs/tests; 2834.1 cover orient paint
