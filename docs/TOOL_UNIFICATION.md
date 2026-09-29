<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tool unification (draft)

Status: **partial** — coordinate path cleaned; Gallery annotation input enabled;
full toolbar/mode merge still open.

## Problem

- Different tool families per mode (Select/Pan/Zoom vs annotation vs crop vs
  attention) with separate action groups and mode checks → messy UI/code.
- Annotation input was Image-mode only; paint already reached Gallery.
- Mouse → page mapping special-cased display rects; **rotates** broke while
  flips worked.

## Coordinate contract (central)

Stored geometry: **page space** (unchanged).

```
view pos
  → scene          (QGraphicsView::mapToScene)
  → item local     (ImageItem::mapFromScene − offset)   // placement / spread scale
  → logical display (scale itemSize → ContentXform::layoutSize)
  → source         (ContentXform::mapDisplayPointToSource)  // flips + turns + crop
  → page           (ThumtooCache::imageRectToPageRect)
```

Inverse for paint: `mapSourcePointToDisplay` then item→scene.

**One** ContentXform path for orient — no separate rotate vs flip branches in
annotation code. Point maps prefer `sourceToDisplayTransform` over 1×1 AABB.

## Modes

| Mode | Annotation paint | Annotation tools |
|------|------------------|------------------|
| Image | yes | yes |
| Gallery | yes | yes (item under cursor) |
| Workspace | yes (tiles) | optional; free pose is harder |

## Still open

- Single tool palette / action group across modes (Select vs Annot-Select).
- Workspace free rotation/shear of items vs content orient — needs testing.
- Rubber-band shapes in Gallery still use view rects (OK for ortho tiles).
