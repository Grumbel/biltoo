# Logical size model

Soft and ladder rasters are **sampling only**. Geometry identity is the path’s
logical size.

## Authority

| Source | Role |
|--------|------|
| `m_imageSizeByPath` / thumtoo / probe | Logical size for a path |
| `ImageItem::m_intrinsicSize` via `setIntrinsicSize` | Item geometry (layout, hit, fit, crop) |
| Soft / ladder / `pixmap()` | Display samples — never identity |

## APIs

| API | Rule |
|-----|------|
| `logicalSizeForPath` | Const lookup — never soft dims |
| `ensureLogicalSizeForPath` | May probe |
| `layoutSizeForPath` | Map / thumtoo / provisional aspect from preview |
| `rememberImageSize` | Never shrink with a smaller sample |
| `rememberSizeFromDecode` | Thumtoo first; ≤2048 long-edge → probe only |
| `ImageItem(path, image)` | Stores sample only; intrinsic starts 1×1 until `setIntrinsicSize` |
| `ImageItem(path, size)` | Placeholder with known logical size |
| `ImageItem::imageSize()` | Intrinsic only |
| `ImageItem::contentRect()` | Intrinsic + offset |
| `ImageItem::paint` | Draw sample into `contentRect` |
| `setSourceImage` | Seed intrinsic if unknown; grow only; never shrink |
| `setIntrinsicSize` | Explicit authority (probe, layout, crop) — always applied |
| `setPreviewImage` | Sample only — no intrinsic change |
| Orientation sync | Soft: transpose aspect; full may grow |

## Forbidden

- Seeding intrinsic from `image.size()` / `pixmap().size()` / soft long-edge
- `contentRect` / pack / fit from display pixel dimensions
- Letting ladder samples block probe or crop via `setIntrinsicSize`
