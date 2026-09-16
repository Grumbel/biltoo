# Logical size model

Soft and ladder rasters are **sampling only**. Geometry identity is the path’s
logical size.

## Authority

| Source | Role |
|--------|------|
| `m_imageSizeByPath` / thumtoo / probe | Logical size for a path |
| `setIntrinsicSize` | Only writer of item geometry (layout, probe, crop, orientation transpose) |
| Soft / ladder / `setSourceImage` / `pixmap()` | Display samples — **never** write intrinsic |

## APIs

| API | Rule |
|-----|------|
| `logicalSizeForPath` | Const lookup — never soft dims |
| `ensureLogicalSizeForPath` | May probe |
| `layoutSizeForPath` | Known logical first; else provisional aspect at **1024** long-edge |
| `rememberSizeFromDecode` | Thumtoo first; ≤2048 long-edge → probe only |
| `setSourceImage` | Sample only — does not touch intrinsic |
| `setPreviewImage` | Sample only |
| `setIntrinsicSize` | Explicit authority — always applied |
| Orientation sync | Transpose aspect only — never adopt sample magnitude |
| `contentRect` / paint | Logical box; sample drawn into it |

## Pure helpers (`imageview_types.h`)

| Helper | Role |
|--------|------|
| `isPositiveSize` | width/height both > 0 |
| `scaleToLongEdge` | provisional aspect at fixed long-edge |
| `isMuchSmallerArea` | reject soft size that would shrink identity |
| `kProvisionalLayoutLongEdge` | 1024 |

## Forbidden

- Seeding or growing intrinsic from sample pixel dimensions
- `layoutSizeForPath` returning raw soft size
- Pack / fit / HUD from `pixmap().size()`

## View framing

Pixel upgrades (soft→full) must not change zoom. Use
`preserveImageViewOnLogicalSizeChange`: refit only when aspect changes; when
only magnitude changes, scale the view so the on-screen footprint stays put.

## Provisional → definitive

| Stage | Geometry |
|-------|----------|
| Cold open (no size) | Neutral stand-in (archives: **1024²** square; else 1000²) + size probe |
| LQIP / soft / thumbnail arrives | **Sample only** — never writes intrinsic or provisional aspect |
| Durable probe / thumtoo size | Authoritative native size; clears provisional; layout settles once |

`layoutSizeForPath` ignores `previewHint` for geometry. Size is the first durable
query and must not jump when LQIP or a ladder sample appears.

Item intrinsic stays **1×1** (or filmstrip square provisional) until `sizeReady`
/ cachedSize; soft install never calls `setIntrinsicSize` from sample dims.

Gallery pack is debounced on aspect change (`requestDebouncedGalleryPack`).

## Display edge vs native

PreferCache / soft climb request edges are **capped at the known native long
edge** (`cappedDisplayEdgeForPath`). Ladder ceil (1024→2048) must not produce a
request larger than the file — a 1920×1080 image never targets 2048.

## Paint

Logical `contentRect` is fixed by the size probe. LQIP / soft / full are textures
only: **stretch samples to the full contentRect**. Do not letterbox previews into
a smaller dest — that looks like the image “grows” when a better sample arrives.

