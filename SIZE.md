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

## Forbidden

- Seeding or growing intrinsic from sample pixel dimensions
- `layoutSizeForPath` returning raw soft size
- Pack / fit / HUD from `pixmap().size()`

## View framing

Pixel upgrades (soft→full) must not change zoom. Use
`preserveImageViewOnLogicalSizeChange`: refit only when aspect changes; when
only magnitude changes, scale the view so the on-screen footprint stays put.
