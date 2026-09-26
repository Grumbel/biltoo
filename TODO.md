# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2704.1-debug-menu` (base `6c3e877`).

### 2704.1 — Debug menu (runtime flags)
- `src/util/debugflags.{h,cpp}` — central on/off flags, seed from env, `qputenv` mirror
- **Debug** menu (before Help): checkable items for overlay, tile, crop, drop,
  find, appearance, filmstrip, slideshow, mode, load, perf, TTFP, GUI budget,
  thumtoo client
- Load/mode/overlay/tile/crop/perf/TTFP paths read `DebugFlags` (not static getenv)
- docs/ENVIRONMENT.md notes the menu

### Prior
2703.4 real size or nothing; 2703.3 cold gallery size book; …

### Apply
```bash
git pull --ff-only …/biltoo-2704.1-debug-menu-6c3e877.bundle HEAD
```
