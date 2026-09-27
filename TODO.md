# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.12-scroll-underlay-floor` (base `bcbb97e`).

### 2714.12 — Fast scroll was blank
Stripping EMB on "warm" virtual slots left no pixels until live items
materialized. Virtual background always paints EMB/LQIP again (tiles still
only on ImageItem via paint_tiles_display).

### Apply
```bash
git pull --ff-only …/biltoo-2714.12-scroll-underlay-floor-bcbb97e.bundle HEAD
```
