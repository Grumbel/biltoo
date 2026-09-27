# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.17-prepare-lqip-repair` (base `bcbb97e`).

### Prepare Tile Cache
- Skipping paths that already have tiles now still runs `ensure_lqip` (kill
  mid-pyramid left tiles without LQIP).
- New pyramid completion also ensures LQIP.
- Dialog stats: tiles / Store LQIP / Store EMB / tiles-without-LQIP / need tiles.
- Progress line includes **LQIP filled this run**.

### Apply
```bash
git pull --ff-only …/biltoo-2714.17-prepare-lqip-repair-bcbb97e.bundle HEAD
```
