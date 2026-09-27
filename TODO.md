# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.20-prepare-ui-explain` (base `bcbb97e`).

### Prepare Tile Cache UI
- Detail levels explained (scale 0/1/2/3 = 1:1 / 2× / 4× / 8×).
- Per-image table: Tiles / Finest scale / LQIP / EMB.
- Overview-only: skips re-encode if finer tiles exist, still fills LQIP.

### Apply
```bash
git pull --ff-only …/biltoo-2714.20-prepare-ui-explain-bcbb97e.bundle HEAD
```
