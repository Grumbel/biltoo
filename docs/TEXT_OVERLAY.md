# Text overlay and Text panel

## Model

- **Source of truth:** `ThumtooCache::PageTextLayer` (regions with bbox + UTF-8 text).
- **Presentation:** `TextLayerSession` flags (`showRegions`, `showGlyphs`, `hoverRegion`,
  `selectedRegions`) — thin overlay state, not a parallel object graph.
- **`TextOverlayState`:** pure-data twin for scripting/plugins (same fields).

Emacs-style idea: the region list is the “buffer”; outlines, glyph paint, hover,
and selection are *properties* of that list, not separate scene items.

## Collaboration

```
PageTextLayer ──► TextPanelModel (list rows)
       │
       └──► TextLayerController paint (outlines / glyphs / selection / hover)
                    ▲
Text panel selection/hover ──signals──┘
```

`TextLayerController` is a `QObject` so panel ↔ page sync uses signals without
MainWindow mediating every index.

## UI

**View → Show Text Panel:** region list, multi-select mirrored to the page,
hover highlight, checkboxes for outlines and “Show text in boxes” (glyphs).

## Future

- Multi-page range in the panel (session slice).
- Scripting API exposing `TextOverlayState` + region indices.
- Hit-test hover from the page into the panel (mouse move over bboxes).
