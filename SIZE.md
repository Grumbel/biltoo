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
| `layoutSizeForPath` | Known logical first; else provisional aspect at **1024** long-edge (never soft pixel size) |
| `imageSizeForPath` | Installs thumtoo into map; provisional neutral if unknown |
| `rememberImageSize` | Never shrink with a smaller sample |
| `rememberSizeFromDecode` | Thumtoo first; ≤2048 long-edge → probe only |
| `ImageItem(path, image)` | Sample only; intrinsic 1×1 until `setIntrinsicSize` |
| `ImageItem::contentRect` / paint | Logical box; sample drawn into it |
| `setIntrinsicSize` | Explicit authority — always applied |

## Forbidden

- Seeding intrinsic or path map from soft/ladder pixel dimensions
- `layoutSizeForPath` returning raw preview size as layout magnitude
- `contentRect` / pack / fit from `pixmap().size()`
