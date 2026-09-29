<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tool unification

Status: **palette unified** — one **Exclusive** `QActionGroup` for Select /
Pan / Zoom / **Crop** / **Attention** + all annotation tools; left Tools strip
shows the full radio; coordinate path cleaned; Gallery annotation input + draft
chrome.

## Canvas tool contract

**One** radio across Image / Gallery / Workspace. Exactly **one** action is
checked at all times (`ExclusionPolicy::Exclusive`).

| Action | ViewInteraction | Annotation::Tool | Crop / Attention | Notes |
|--------|-----------------|------------------|------------------|-------|
| Select | `Tool::Select` | `None` | off | Items / gallery cells / text regions |
| Pan | `Tool::Pan` | `None` | off | Shortcut **H** (GIMP Hand) |
| Zoom | `Tool::Zoom` | `None` | off | Workspace rubber-band zoom |
| Crop | (unchanged) | `None` | **crop on** | Gallery may open Image first |
| Attention | `Tool::Select` | `None` | **attention on** | Image-only; Shift+A |
| Annotation Select | `Tool::Select` | `Select` | off | Markup objects only |
| Pen / Highlighter / … | `Tool::Select` | matching | off | View tool stays Select underneath |

### Activation path

1. Toolbar / shortcut → `QAction::triggered` → `QActionGroup` → `onCanvasToolTriggered`
2. Programmatic → `setSelectTool` / `setPanTool` / `setZoomTool` / `toggleCropMode` / `toggleAttentionMode` → checked + handler
3. Mode chrome → `syncCanvasToolChrome()` (priority: **crop > attention > annot > view tool**)

**Crop specifics**

- Enter: `enterCropFromCanvasTool()` (clears attention + annot; Gallery defers until Image has display pixels, holding Select in the radio until then).
- Exit: choose Select / Pan / Zoom / annot tool, **or** re-select Crop / press `C` while cropping (toggle-off → **mode default**: Pan in Image, Select in Gallery/Workspace via `activateDefaultViewTool`).

**Attention (in the Exclusive radio)**

- Image-only mode entry (`Shift+A` / Attention on the Tools strip). Member of the same
  Exclusive group as Select / Pan / Zoom / Crop / annotation tools.
- Enter: cancels crop, clears annotation tool, `setTool(Tool::Select)`, then
  `hostAttention().setAttentionMode(true)`. From Gallery/Workspace, opens the
  current session index in Image mode first.
- Exit: re-select Attention (toggle-off → mode default), Escape, mode switch,
  or any other canvas-tool choice.

### Why Exclusive (not Optional)

Optional allowed the active tool to uncheck on re-click and left Select/Pan/Zoom
chrome stuck. Exclusive matches a normal tool radio. Crop / Attention “toggle off”
is implemented explicitly when the same action is re-selected while already active.

### Shortcuts (tool-relevant)

| Key | Action |
|-----|--------|
| `V` | Select |
| `H` | Pan (Hand) |
| `Z` | Zoom |
| `C` | Crop |
| `Shift+A` | Attention |
| `Shift+H` | HUD overlay toggle (not a canvas tool) |

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
