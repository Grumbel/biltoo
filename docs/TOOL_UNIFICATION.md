<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tool unification

Status: **palette unified** — one **Exclusive** `QActionGroup` for Select /
Pan / Zoom + all annotation tools; coordinate path cleaned; Gallery annotation
input + draft chrome.

## Canvas tool contract

**One** radio across Image / Gallery / Workspace. Exactly **one** action is
checked at all times (Exclusive, not Optional — Optional let the active tool
uncheck on re-click and left Select/Pan/Zoom chrome stuck when switching).

| Action | ViewInteraction | Annotation::Tool | Notes |
|--------|-----------------|------------------|-------|
| Select | `Tool::Select` | `None` | Items / gallery cells |
| Pan | `Tool::Pan` | `None` | |
| Zoom | `Tool::Zoom` | `None` | Workspace rubber-band zoom |
| Annotation Select | `Tool::Select` | `Select` | Markup objects only |
| Pen / Highlighter / … | `Tool::Select` | matching | View tool stays Select underneath |

Handler: `MainWindow::onCanvasToolTriggered`. Forces a single checked action
(belt-and-suspenders with the Exclusive group). Leave annotation by choosing
Select / Pan / Zoom.

Crop and Attention remain **modes** outside the radio (Gallery→Image entry,
async load). Activating any canvas tool exits crop; attention is toggled
separately and still clears crop on entry.

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

- Workspace free rotation/shear of items vs content orient — needs testing.
- Rubber-band shapes in Gallery still use view rects (OK for ortho tiles).
- Optional: fold Crop / Attention into the same radio if mode-entry side
  effects can be expressed as tool activation without special cases.
