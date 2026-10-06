# TODO / agent handoff

## Status (2026-10-07)

**Tip:** tile loading rewrite — explicit state machine (Claude Code, direct
commits on master; no bundle).

### Tile loading state machine
Normative: [docs/TILE_STATE_MACHINE.md](docs/TILE_STATE_MACHINE.md) (includes
the full audit of the old design, B1–B15 / T1–T7). Per-path `TileLoader`
owns cell states and the only request per key; `TileSession` per view
publishes demand (lease) and reports `Phase` + reason; `TileScheduler` is the
sole, event-driven issuer. Failures show on the item ("Showing lower
resolution: …" / "Tiles failed: …"). Tiles wait for the authoritative native
size (`cachedSize`).

Open follow-ups:
- Manual QA of the TILE_LOD_RUNTIME checklist in the real GUI (zoom/pan,
  Workspace duplicates, slideshow, filmstrip, PDF denser on scans).
- `flake.lock` still pins an older thumtoo; bump after thumtoo master with
  `request_tile_cells` is pushed (`nix build` needs it).

### Depends on
thumtoo `183d053` (request_tile_cells) or later.

### Earlier: 2892.16

### 2892.16
- View → Smooth Scaling drives thumtoo::set_smooth_image_scaling + invalidateAll.

### Depends on
thumtoo-040.2-smooth-image-scale (set_smooth_image_scaling API).

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
