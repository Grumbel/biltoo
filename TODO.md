# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.13-pdf-image-heavy-denser (linear stack on `a889409`).

### 2892.13
- Root cause of FAILED denser on many PDFs: thumtoo refuses scale<0 for
  image-heavy pages (scans). denserLiveDenied memo + min_scale floor 0.

### Open follow-up
- PreferCache/Full climb when denser denied and user zooms past layout dpi
  (tiles own display still skips PreferCache).

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
