# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2672.1-tiles-not-lqip-under-256` (base `2e49220`).

### This tip
`shouldUseTiles` no longer treats layout long edge < 256 as "no tiles"
(that blocked crops / small boxes while file-native 256² tiles exist).
Screen footprint > ~32px wants tiles; LQIP underlay only. Tiny *native*
files (< 256) still skip the pyramid.

### Apply
```bash
git pull --ff-only …/biltoo-2672.1-tiles-not-lqip-under-256-2e49220.bundle HEAD
```
