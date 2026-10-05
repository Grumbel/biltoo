# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.2-slideshow-wake-null (linear stack on `a889409`).

### 2892.2
- Slideshow tile wake: tick via `QPointer<SlideshowController>` +
  `tickSlideshowTileLod` (no `hostDisplayPipeline` null-deref warning).

### 2892.1
- Image/Slideshow coarse-tile climb: always install wake; slideshow pumps
  on completion.

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
