# TODO / agent handoff

## Status (2026-10-07)

**Tip:** source records + page profiles + Status panel (Claude Code, direct
commits on master; no bundle).

### Source records, page profiles, Status panel
Normative: [docs/SOURCE_RECORDS.md](docs/SOURCE_RECORDS.md). PDF pages get a
thumtoo page profile (vector / raster / mixed, image dpi); the zoom floor
comes from it (`decide_zoom_floor`) instead of stepping down after
Unavailable answers (ProcessMemos denser floor removed). Every heuristic is
recorded as a decision with its reason. Panels → Status shows page analysis,
decisions, tile state and thumtoo decode counts; `BILTOO_STATUS_REPORT=<file>`
dumps it. Denser targets demand the same T+1/T+2 overview as rasters.

Open follow-ups:
- Manual GUI QA: zoom into the benchtoo `pdf-classes` (scan stops at its
  cap, vector/mixed pages to −4, Status panel values); real-world PDFs.
- DjVu / EPUB have no profile yet (floor −4, no decode stats).

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
  the PDF rendering rewrite is pushed (`nix build` needs it).

### Depends on
thumtoo `28d209c` (PDF rendering rewrite: pdf_page_profile,
pdf_document_render_stats, PdfCellRender) or later.

### Earlier: 2892.16

### 2892.16
- View → Smooth Scaling drives thumtoo::set_smooth_image_scaling + invalidateAll.

### Depends on
thumtoo-040.2-smooth-image-scale (set_smooth_image_scaling API).

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
