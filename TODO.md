# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.12-underlay-seed-ensure-lqip` (base `b9c3473`).

### 2713.12 — Underlay seed: tiles-without-LQIP recovery
- `cachedLqipImage`: if Store EMB/LQIP miss but durable tiles exist, call
  `ensure_lqip` (free TileSynth data only — fills holes from quit mid-pyramid)
- GUI path uses `getUnderlay` only (never main soft slot)

### Pair with thumtoo
**thumtoo-344.2** — SizeReply warm path includes EMB (`get_embedded_preview`).
Hot-cache size probes were returning size without PDF/EXIF thumbs because
`get_lqip` excludes EmbeddedJpeg.

### Apply
```bash
git pull --ff-only …/biltoo-2713.12-underlay-seed-ensure-lqip-b9c3473.bundle HEAD
git pull --ff-only …/thumtoo-344.2-sizereply-embedded-aeb5159.bundle HEAD
```

## Next
True single-SQL size+underlay join in Store (optional); Resolving sizes should
look uniform once EMB rides on SizeReply.

## Roadmap / later
### Kill Soft / Tile LOD
See docs/KILL_SOFT.md, docs/TILE_DRAW_INVESTIGATION.md
