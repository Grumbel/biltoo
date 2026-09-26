# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.7-lqip-underlay-install` (base `b9c3473`; linear stack).

### 2713.7 — LQIP underlay install/paint reliability
- `tryInstallGalleryUnderlay`: soft on item is not “done”; only EMB/LQIP-band
- Paint: soft under tiles → placeholder (not blank hole)
- `setHasLqip`: also ImageCache emb-band (plan U when cache has LQIP)

### Apply
```bash
git pull --ff-only …/biltoo-2713.7-lqip-underlay-install-b9c3473.bundle HEAD
```

## Prior stack
2713.1–2713.6 (tile debug, Kill Soft A–D, unused lambda)

## Next
- Runtime re-check `plan=…/H` and `lqip=1` after Store seed
- If LQIP still missing cold: thumtoo opportunistic LQIP from first coarse tile

## Roadmap / later
### Tile draw / LOD investigation
Plan: [docs/TILE_DRAW_INVESTIGATION.md](docs/TILE_DRAW_INVESTIGATION.md)

### Kill Soft
[docs/KILL_SOFT.md](docs/KILL_SOFT.md) — A–D done

### Tags and bookmarks
Scope brainstorm: [docs/TAGS_AND_BOOKMARKS.md](docs/TAGS_AND_BOOKMARKS.md)
