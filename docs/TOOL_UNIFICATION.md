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
checked at all times (`ExclusionPolicy::Exclusive`).

| Action | ViewInteraction | Annotation::Tool | Notes |
|--------|-----------------|------------------|-------|
| Select | `Tool::Select` | `None` | Items / gallery cells |
| Pan | `Tool::Pan` | `None` | |
| Zoom | `Tool::Zoom` | `None` | Workspace rubber-band zoom |
| Annotation Select | `Tool::Select` | `Select` | Markup objects only |
| Pen / Highlighter / … | `Tool::Select` | matching | View tool stays Select underneath |

### Activation path

1. Toolbar / shortcut → `QAction::triggered` → `QActionGroup` → `onCanvasToolTriggered`
2. Programmatic → `setSelectTool` / `setPanTool` / `setZoomTool` → `setChecked(true)` + `onCanvasToolTriggered` (not `trigger()`, which toggles a checked Exclusive action)
3. Mode chrome → `syncCanvasToolChrome()` from `updateWorkspaceActionVisibility` (mirrors controller state; signal-blocked so it does not re-enter the handler)

`onCanvasToolTriggered` forces a single checked action, exits crop, sets
`ViewInteraction` and/or `Annotation::Tool`, restores cursor.

Leave annotation by choosing Select / Pan / Zoom.

Crop and Attention remain **modes** outside the radio (Gallery→Image entry,
async load).

### Why not ExclusiveOptional

Optional allowed the active tool to uncheck on re-click. Combined with a
fallback that re-checked Select, Select/Pan/Zoom chrome could stick or show
multiple pressed buttons when switching quickly. Exclusive matches a normal
tool radio (Photoshop, Okular).

## Coordinate contract (central)

Stored geometry: **page space** (unchanged).

```
view pos
  → scene          (QGraphicsView::mapToScene)
  → item local     (ImageItem::mapFromScene − offset)
  → logical display (scale itemSize → ContentXform::layoutSize)
  → source         (ContentXform::mapDisplayPointToSource)
  → page           (ThumtooCache::imageRectToPageRect)
```

Inverse for paint: `mapSourcePointToDisplay` then item→scene.

## Modes

| Mode | Annotation paint | Annotation tools |
|------|------------------|------------------|
| Image | yes | yes |
| Gallery | yes | yes (item under cursor) |
| Workspace | yes (tiles) | optional; free pose is harder |

## Still open

- Workspace free rotation/shear of items vs content orient — needs testing.
- Rubber-band shapes in Gallery still use view rects (OK for ortho tiles).
- Optional: fold Crop / Attention into the same radio.
