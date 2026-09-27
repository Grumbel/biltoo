# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.14-tiles-over-emb` (base `bcbb97e`).

### Root causes (EMB while tiles exist)
1. `tileLodActive()` ignored retained path RAM → underlay-only frame.
2. `prepareTileLodPlan` ran *after* underlay decision.
3. `prepare_and_paint_cover` refused retained tiles via 32px `shouldUseTiles`.
4. EMB put could replace LQIP in underlay map without care.

### Fixes
- `tileLodActive` = any tile **or** retained registry tiles.
- Prepare plan before underlay; path-RAM aware debug tags.
- Cover paint always allows retained tiles (min-scale scroll floor).
- Underlay put refined for LQIP vs EMB.
- **docs/GALLERY_PIXELS.md** rewritten as priority authority.

### Apply
```bash
git pull --ff-only …/biltoo-2714.14-tiles-over-emb-bcbb97e.bundle HEAD
```
