# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2833.3-tile-settled-no-reissue (on 2833.2 stack).

### 2833.3
- TileLoadCoordinator: treat `coverage().settled()` (all Succeeded **or Failed**)
  as covered — stop infinite zero-tile re-issue when interactive tiles Failed
  for the generation (archive gallery stuck on LQIP + tile-coord spam).
- `TileLodController::viewportSettled` / `ImageItem::tileLodSettled`

### 2833.2
- Fix: `filmstripSurfaceTick` declares `needSchedule`
- Quiet idle `BILTOO_TILE_DEBUG` gallery-decode (0,0) samples

### 2833.1 Tool unification
- Attention on left Tools strip; HUD → Shift+H; Pan keeps H

### Required thumtoo
thumtoo-008.1-markdown-cmark-mutex-3e6987f.bundle

### Note on archive tiles Failed
If cells stay LQIP with `exact=0 cacheOk=0` after this tip, tiles are **failing**
in thumtoo (not a host re-issue loop). Check `THUMTOO_DEBUG=1` / prepare tiles
for the archive; host will not poll Failed keys until viewport/generation bumps.

### Possible next
- Limited Failed-key retry after store encode (generation bump cooldown)
- Text Highlighter → Mark selection (design only)
