# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.2-kill-soft-phase-a` (base `b9c3473`; stacks on 2713.1).

### 2713.2 — Kill Soft Phase A (policy + rename)
- Decision: [docs/KILL_SOFT.md](docs/KILL_SOFT.md)
- `ClimbPolicy::SoftDisplay` → **`TileDisplay`** (TileSynth / pyramid / overview; no Full)
- Soft ladder is product-dead; Gallery already LQIP+tiles; Image/Workspace next

### 2713.1 — Tile draw Phase 1: plan histogram + contracts
- [docs/TILE_DRAW_INVESTIGATION.md](docs/TILE_DRAW_INVESTIGATION.md) §13
- `BILTOO_TILE_DEBUG` plan=E/P/U/H + lqip flag

### Apply
```bash
git pull --ff-only …/biltoo-2713.2-kill-soft-phase-a-b9c3473.bundle HEAD
```

## Next (Kill Soft)
- **Phase B:** collapse climb `scheduleSoft` SoftOnly vocabulary; tests
- **Phase C:** Image/Workspace underlay = EMB/LQIP only under tiles
- **Phase D (thumtoo):** PreferCache miss → coarse tiles preferred over soft encode

## Roadmap / later
### Tile draw / LOD investigation
Plan: [docs/TILE_DRAW_INVESTIGATION.md](docs/TILE_DRAW_INVESTIGATION.md)

### Tags and bookmarks
Scope brainstorm: [docs/TAGS_AND_BOOKMARKS.md](docs/TAGS_AND_BOOKMARKS.md)

### Feature brainstorm
[docs/FEATURE_BRAINSTORM.md](docs/FEATURE_BRAINSTORM.md)

### Text-to-speech
[docs/TEXT_TO_SPEECH.md](docs/TEXT_TO_SPEECH.md)
