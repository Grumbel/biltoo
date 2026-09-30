# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2834.1-cover-orient-shared-paint (on 2833.4 stack).

### 2834.1
- `prepare_and_paint_cover` accepts `ContentXform::Value xform`.
  Identity → prior paint_draw_plan cover; orient/crop/flip →
  `paint_tiles_display` (same plan/registry as ImageItem).
- Gallery virtual slots + filmstrip pass session appearance into cover args.
- Filmstrip content rect was already layout-oriented; tiles now rotate with it.
- Docs: FILMSTRIP_LAYOUT, tile_display_paint.hpp comments.

### Required thumtoo
thumtoo-010.1-tile-supersede-activity-finish (on 009.2; fixes stuck
`tile=N/0` Working badge from single-cell supersede activity leak)

### Note
`status=Working` with `activity tile=N/0` and thumtoo pending=0 was a ledger
leak, not real work. Apply 010.1.

### Still open (tool unification design)
- Text Highlighter → Mark selection (ANNOTATION_OVERLAY §14; do not rush)
