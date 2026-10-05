# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.8-denser-need-s0 (linear stack on `a889409`).

### 2892.8
- Denser held at tgt=-1 with exact=0 inflight=0: coarser pathRam passed
  scale_ge(0) gate without layout cells. Require exact s=0 before denser;
  re-plan at 0 when denser batch empty without s=0.
- TILE_DEBUG: st= min= miss= fail= s0=

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
