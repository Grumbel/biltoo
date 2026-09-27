# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.2-tile-size-change-clear` (base `bcbb97e`).

### 2714.2 — Clear tile RAM when content size changes
- `TileSession::set_content_size`: if native WxH changes after a prior size,
  clear the shared path `TileMemoryCache` so old-scale cells are not painted
  into the new `tile_content_rect` grid (stretch / wrong-scale symptom).
- First bind (0×0 → real) does **not** clear retained same-file path tiles.
- Unit test: `test_set_content_size_clears_stale_grid`.

### 2714.1 — F5 / Shift-F5 (included in this tip stack)
- Soft F5: mtime-gated process regenerate; hard: durable purge.
- Selection / primary only; no Gallery relayout; no PendingSessionBind/LoadAdd.

### Apply
```bash
git pull --ff-only …/biltoo-2714.2-tile-size-change-clear-bcbb97e.bundle HEAD
```
