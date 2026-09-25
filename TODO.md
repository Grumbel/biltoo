# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2702.2-verify-nonsticky-scale` (base `932ed5c`).

### 2702.2 — Verify + non-sticky identity scale
Review of 2702.1:
- Edge-stretch coverage math OK (full native AABB).
- Updated `test_content_coverage_odd_widths` for stretch semantics.
- Non-sticky Image framing also forced identity item scale (invalid preserved
  scale + default Fit still called fitItem with Gallery pack scale).

Latent (not fixed): `tile_level_rect` clamps negative scales to 0 — PDF
negative pyramid keys would mis-map if Image ever requests them.

### 2702.1 — Coarse tile dest stretch + Image framing scale
### 2701.1 — Gallery wrong scale / soft F5 no-op
### 2700.1 — Sticky zoom survives Gallery

### Apply
```bash
git pull --ff-only …/biltoo-2702.2-verify-nonsticky-scale-932ed5c.bundle HEAD
```
