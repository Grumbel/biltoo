# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2823.2-gallery-minscale-density on 2823.1 stack.

### 2823.2 Gallery min_scale = density
- Gallery no longer floors requests on `durableTileMinScale` (Store is cache, not limit)
- `min_scale` = dens from screen dpc + screen-edge hard floor
- Inspection zoom can interactive encode-on-miss below stored finest

### 2823.1
- Hard floor uses screen long edge (not scene)

### Required thumtoo
thumtoo-007.1-materialize-tile-cell-3e6987f.bundle
