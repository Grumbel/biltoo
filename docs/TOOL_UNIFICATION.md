<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tool unification

Status: **palette unified** — one **Exclusive** `QActionGroup` for Select /
Pan / Zoom / **Crop** + all annotation tools; coordinate path cleaned; Gallery
annotation input + draft chrome.

## Canvas tool contract

**One** radio across Image / Gallery / Workspace. Exactly **one** action is
checked at all times (`ExclusionPolicy::Exclusive`).

| Action | ViewInteraction | Annotation::Tool | Crop mode | Notes |
|--------|-----------------|------------------|-----------|-------|
| Select | `Tool::Select` | `None` | off | Items / gallery cells |
| Pan | `Tool::Pan` | `None` | off | |
| Zoom | `Tool::Zoom` | `None` | off | Workspace rubber-band zoom |
| Crop | (unchanged) | `None` | **on** | Gallery may open Image first |
| Annotation Select | `Tool::Select` | `Select` | off | Markup objects only |
| Pen / Highlighter / … | `Tool::Select` | matching | off | View tool stays Select underneath |

### Activation path

1. Toolbar / shortcut → `QAction::triggered` → `QActionGroup` → `onCanvasToolTriggered`
2. Programmatic → `setSelectTool` / `setPanTool` / `setZoomTool` / `toggleCropMode` → checked + handler
3. Mode chrome → `syncCanvasToolChrome()` (priority: crop > annot > view tool)

**Crop specifics**

- Enter: `enterCropFromCanvasTool()` (clears attention + annot; Gallery defers until Image has display pixels, holding Select in the radio until then).
- Exit: choose Select / Pan / Zoom / annot tool, **or** re-select Crop / press `C` while cropping (toggle-off → **mode default**: Pan in Image, Select in Gallery/Workspace via `activateDefaultViewTool`).

**Attention (in the Exclusive radio)**

- Image-only mode entry (`Shift+A` / Attention action). Member of the same
  Exclusive group as Select / Pan / Zoom / Crop / annotation tools.
- Enter: cancels crop, clears annotation tool, `setCurrentTool(Select)`, then
  `hostAttention().setAttentionMode(true)`. From Gallery/Workspace, opens the
  current session index in Image mode first.
- Exit: re-select Attention (toggle-off → mode default), Escape, mode switch,
  or any other canvas-tool choice.

### Why Exclusive (not Optional)

Optional allowed the active tool to uncheck on re-click and left Select/Pan/Zoom
chrome stuck. Exclusive matches a normal tool radio. Crop “toggle off” is
implemented explicitly when Crop is re-selected while already active.

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

## Still open

- **Text Highlighter tool vs Mark selection** — design note in
  [ANNOTATION_OVERLAY.md §14](ANNOTATION_OVERLAY.md); do not remove the tool yet.
- Workspace free rotation/shear of items vs content orient — needs testing.
- Rubber-band shapes in Gallery still use view rects (OK for ortho tiles).
- Shortcut clash: Pan and HUD both claim `H` (GIMP-style hand vs HUD toggle).
