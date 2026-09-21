<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Layout ground truth

## Rule

**Content layout size** for any presentation surface (filmstrip cell, Gallery
pack cell, Image underlay, Workspace free-form tile):

```
native = logicalSizeForPath(path)   // file pixels, never soft sample
want   = ItemWorld for SessionImageId when durable
       | else XDG orient/flip for path (bound: orient only, no path crop)
layout = ContentXform::layoutSize(native, want)
```

API: `ImageView::contentLayoutSize(path, sessionId)`.

`layoutSizeForPath` returns **file-native only**. Use it only as the native
input to `ContentXform::layoutSize`, or via `contentLayoutSize`.

## Surfaces (must use contentLayoutSize or equivalent)

| Surface | Status |
|---------|--------|
| Filmstrip `LayoutAspectProvider` | → `contentLayoutSize` |
| Gallery ensurePlaceholders | `contentLayoutSize` |
| setWorkspacePaths placeholders | `contentLayoutSize` |
| Workspace placeOrMove | `contentLayoutSize` (+ XDG seed into ItemWorld) |
| Image force underlay | `contentLayoutSize` |
| Image pendingTile placeholder | `contentLayoutSize` |
| Live install | `applyContentLayoutSize` + `wantAppearanceForItem` |
| createItemFromImage | native + `appearanceForNewImageModeItem` → layoutSize |
| Duplicate selection | native + content state → layoutSize |
| sizeReady / applyProbedImageSize | native probe + wantAppearanceForItem |

## Anti-patterns

- Override / soft **pixmap size** as aspect
- Path crop as layout for **bound** `SessionImageId` rows
- `layoutSizeForPath` alone as pack/placeholder intrinsic for oriented rows
- Tile LOD treating oriented `imageSize()` as file-native (`tileNativeSize`)
