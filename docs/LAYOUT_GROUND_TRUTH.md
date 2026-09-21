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
want   = ItemWorld appearance for SessionImageId  // content bake + crop
layout = ContentXform::layoutSize(native, want)
```

`layoutSizeForPath` returns **file-native only**. Do not use it as intrinsic
size when orient/crop may apply — use `ImageView::contentLayoutSize(path, sid)`.

## Surfaces

| Surface | API |
|---------|-----|
| Filmstrip | `LayoutAspectProvider` → same formula via MainWindow |
| Gallery pack / ensurePlaceholders | `contentLayoutSize` |
| Workspace placeOrMove | `contentLayoutSize` (+ XDG seed into ItemWorld if needed) |
| Image underlay force | `contentLayoutSize` |
| Live install | `applyContentLayoutSize` after `wantAppearanceForItem` |

## Anti-patterns

- Override / soft **pixmap size** as aspect
- Path-only XDG as authority when `SessionImageId` is bound
- `layoutSizeForPath` as pack/placeholder intrinsic for oriented session rows
- Tile LOD using oriented `imageSize()` as file-native (see `tileNativeSize`)
