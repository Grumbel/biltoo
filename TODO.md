# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.3-pdf-tile-native-size (linear stack on `a889409`).

### 2892.3
- PDF/page/archive: `tileNativeSize()` never uses soft `imageSize()`; wait for
  ProcessMemos/SizeReply. Soft size as grid caused Failed denser + PARENT.
- TileSession: backoff `min_scale` when all denser (scale<0) cells Failed.

### 2892.1–2892.2
- Tile climb wake; slideshow wake null warning.

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
