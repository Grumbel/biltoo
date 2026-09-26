# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.4-kill-soft-b-c` (base `b9c3473`; linear stack).

### Stack (oldest → newest)
1. **2713.1** Tile draw Phase 1 plan histogram + contracts
2. **2713.2** Kill Soft Phase A — TileDisplay rename + decision doc
3. **2713.3** Kill Soft Phase B — climb `scheduleBand` vocabulary
4. **2713.4** Kill Soft Phase C — EMB/LQIP underlay only under tiles

### Apply
```bash
git pull --ff-only …/biltoo-2713.4-kill-soft-b-c-b9c3473.bundle HEAD
```

## Next
- **Kill Soft Phase D (thumtoo):** PreferCache miss → coarse tiles preferred over soft encode
- Tile draw: runtime R1–R3 with `BILTOO_TILE_DEBUG=1`

## Roadmap / later
### Tile draw / LOD investigation
Plan: [docs/TILE_DRAW_INVESTIGATION.md](docs/TILE_DRAW_INVESTIGATION.md)

### Kill Soft
[docs/KILL_SOFT.md](docs/KILL_SOFT.md) — A–C done; D optional thumtoo

### Tags and bookmarks
Scope brainstorm: [docs/TAGS_AND_BOOKMARKS.md](docs/TAGS_AND_BOOKMARKS.md)

### Feature brainstorm
[docs/FEATURE_BRAINSTORM.md](docs/FEATURE_BRAINSTORM.md)

### Text-to-speech
[docs/TEXT_TO_SPEECH.md](docs/TEXT_TO_SPEECH.md)
