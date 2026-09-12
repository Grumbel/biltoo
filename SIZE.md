# Logical size model

Soft and ladder rasters are **sampling only**. Geometry identity is the path’s
logical size.

## Authority

| Source | Role |
|--------|------|
| `m_imageSizeByPath` / thumtoo / probe | Logical size for a path |
| `ImageItem::m_intrinsicSize` | Item’s logical size (layout, hit, fit) |
| Soft / ladder / `pixmap()` | Display samples — never identity |

## APIs

| API | Rule |
|-----|------|
| `logicalSizeForPath` | Const lookup — never soft dims |
| `ensureLogicalSizeForPath` | May probe |
| `rememberImageSize` | Never shrink with a smaller sample |
| `rememberSizeFromDecode` | Thumtoo first; ≤2048 long-edge → probe only |
| `ImageItem::imageSize()` | Intrinsic only |
| `ImageItem::contentRect()` | Intrinsic + offset |
| `ImageItem::paint` | Draw sample into `contentRect` |
| `setSourceImage` | Seed if unknown; grow only |
| `setPreviewImage` | Sample only — no intrinsic change |
| Orientation sync | Soft: transpose aspect; full may grow |

## Forbidden

- `rememberImageSize(path, soft.size())`
- `contentRect` / pack / fit from `pixmap().size()`
- Adopting ladder dimensions as native when a larger logical size is known
