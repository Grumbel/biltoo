# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2877.1-overlay-device-text (on `ea477d6` + agent stack).

### 2877.1
- Tile plan overlay text drawn in **device space** at fixed 11/14 px (no
  logical px / sx). Fixes FreeType `render glyph failed err=62` from huge
  setPixelSize when zoomed out.

### Prior
- 2876.1 overlay text size (1/sx approach — caused FreeType err)
- 2875.1 tile scroll cancel

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
