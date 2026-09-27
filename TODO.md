# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.8-shared-tile-plan-overlay` (base `bcbb97e`).

### 2714.8 — Same TILE s=N COMPLETE overlay on virtual slots
- Extracted `paintTilePlanDebugOverlay` to `tilelod/tile_plan_debug_overlay.*`
  (shared by ImageItem + Gallery).
- Virtual warm-tile paint no longer stamps a special-case `"TILE"` string —
  uses the real plan overlay (Exact/Parent washes + TILE / s=N / COMPLETE).

### Prior (included)
2714.7 virtual tiles · 2714.6 orient · 2714.5–1 …

### Apply
```bash
git pull --ff-only …/biltoo-2714.8-shared-tile-plan-overlay-bcbb97e.bundle HEAD
```
