# Gallery / display pixels

**LQIP underlay + grid tiles.** SoftOnly / PreferCache whole-frame is not used
when tiles can own the path.

| Mode | Policy |
|------|--------|
| Gallery | LQIP + tiles; no SoftOnly job; soft want ≤ LQIP for non-tile cells |
| Workspace | Same as Gallery for classic decode |
| Image mode | LQIP + tiles when `tileLodWanted` or durable tiles known |
| Slideshow | SoftOnly only on **cold** paths (no durable, no adequate cache) |

See also `docs/TILE_LOAD_COORDINATOR.md`.
