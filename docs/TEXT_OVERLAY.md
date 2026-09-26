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

## Layout analysis (Kind)

| Kind | How it is assigned today |
|------|---------------------------|
| **Body** | Default |
| **Header** | OCR post-pass: region centre in top ~8% of page |
| **Footer** | OCR post-pass: centre in bottom ~8% |
| **PageNumber** | Header/footer band + short numeric/roman token, outer or centred |
| **Link** | Role=Link (native PDF), not a Kind |

**Not implemented:** true headings (font-size/style), columns, reading-order
clusters beyond `blockId`, native-PDF Kind annotation (usually all Body).

Paint and the Text panel use `TextRegionStyle` (shared colours/labels).
