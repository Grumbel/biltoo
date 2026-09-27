# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.10-virtual-cold-only-verified` (base `bcbb97e`).

### Architecture (verified)

| Layer | Owner | What |
|-------|--------|------|
| Warm tiles | `ImageItem` + `TileLodController` | Only tile paint + `paintTilePlanDebugOverlay` |
| Cold underlay | `paintVirtualPlaceholders` | LQIP / EMB / placeholder when **no live item** |
| Upright EXIF | `ImageCache::matchNativeAspect` | Shared |
| Debug overlay | `tilelod/tile_plan_debug_overlay.*` | Shared |

### 2714.10
- Virtual paint **skips slots that already have a live item** (was redrawing under them).
- Removed warm-only dark-fill special case; warm lag uses LQIP if any, else chrome.
- `syncVirtualWindow` still prioritizes creating live items for warm on-screen paths.

### Apply
```bash
git pull --ff-only …/biltoo-2714.10-virtual-cold-only-verified-bcbb97e.bundle HEAD
```
