# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.5-overlay-fast-scroll` (base `bcbb97e`).

### 2714.5 — Tile plan overlay visible during fast Gallery scroll
- Fast scroll keeps most cells as **virtual placeholders** (no ImageItem) —
  paint sample kind tags on those when Overlay/TileDebug is on.
- `ItemCoordinateCache` froze live paint() → bare LQIP while scrolling; force
  **NoCache** whenever the debug overlay is enabled (scroll cache + paint gate).
- Toggling Overlay/TileDebug invalidates live item caches immediately.

### Prior (included)
2714.4 Gallery return viewport · 2714.3 size memo · 2714.2 tile size · 2714.1 F5

### Apply
```bash
git pull --ff-only …/biltoo-2714.5-overlay-fast-scroll-bcbb97e.bundle HEAD
```
