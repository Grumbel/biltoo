<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Annotation overlay — research and design

Status: **Phase C (in tree)** — freehand/text highlight, pen, eraser, select, shapes, sticky notes, project save/load, flattened PNG export.
Page-space coords aligned with text overlays. formatVersion 2 envelope (string kinds, decimal ids).

Related: [CONTENT_COORDINATES.md](CONTENT_COORDINATES.md),
[OCR_COORDINATES.md](OCR_COORDINATES.md), [TEXT_OVERLAY.md](TEXT_OVERLAY.md)
(if present), [TAGS_AND_BOOKMARKS.md](TAGS_AND_BOOKMARKS.md),
[FEATURE_BRAINSTORM.md](FEATURE_BRAINSTORM.md).

---

## 1. Goal

Add a **session-owned annotation layer** drawn over page/image content so the
user can:

- Mark up pages with **highlighter**, **pen/ink**, **shapes**, and optional
  **text notes**
- **Highlight specific text runs** using the existing text layer (native or OCR)
- Keep **black (or dark) text legible** through translucent colour (true
  highlighter behaviour, not a solid wash)

Annotations are **view chrome on top of content**, not a destructive bake into
the source file in v1. Export / PDF write-back can come later.

---

## 2. What similar apps actually ship

| App | Strength | Tool set (typical) | Highlight model | Persistence |
|-----|----------|--------------------|-----------------|-------------|
| **Adobe Acrobat** | Industry PDF /Annot | Highlight, underline, strikeout, squiggly, sticky note, free text, ink, line, square, circle, polygon, stamp | Text-snapped (QuadPoints) | PDF annotation objects |
| **Okular** | KDE document viewer | Text highlighter + graphic: freehand, highlighter, line, ellipse, polygon, stamp, typewriter, popup/inline note | Text tools on PDF text; graphic tools on any format | PDF and/or `.okular` |
| **Xournal++** | Stylus + PDF underlay | Pen, **highlighter** (separate tool), eraser, shapes, text, LaTeX; PDF text select → highlight/underline/strike | Freehand highlighter *and* text-derived strokes | Own format; flatten on PDF export |
| **Apple Preview / Xodo / Zotero reader** | Review workflows | Highlight, notes, ink, basic shapes | Text-snapped where a text layer exists | PDF /Annot |
| **GoodNotes / Notability** | Handwriting notebooks | Pens, highlighters, shapes | Mostly freehand over the page | Proprietary; flatten on export |

### Patterns that matter for biltoo

1. **Two highlight paths**
   - **Text-snapped**: select glyphs/regions from the text layer → store quads
     or region ids. Best readability and “select a sentence” UX (Acrobat, Okular
     text tools, Xournal++ PDF text tools).
   - **Freehand highlighter**: wide translucent stroke (Xournal++ Pen vs
     Highlighter). Works on pure images and when OCR is missing.

2. **Tool separation**: Pen (opaque or semi-opaque ink) vs Highlighter
   (multiply / special blend) vs Eraser vs Select. Mixing “one brush with
   opacity slider” is weaker than a dedicated highlighter tool.

3. **Standard PDF subtypes** (ISO 32000) are a good *vocabulary* even if v1
   does not write PDF: Highlight, Underline, Squiggly, StrikeOut, Ink, Square,
   Circle, Line, FreeText, Text (sticky).

4. **Non-destructive overlay** first; optional flatten/export later (Xournal++
   model). biltoo already separates content vs chrome (crop, grade, text
   regions).

---

## 3. Blend modes — “black text shines through”

### Problem

A yellow rectangle at 40% alpha **over** black text still tints the glyphs gray
if the blend is ordinary SourceOver. Real highlighters work more like a
**marker on paper**: paper and ink both remain dark where the ink was.

### Useful Qt / GPU modes

| Mode | Effect on dark text under light colour | Use |
|------|----------------------------------------|-----|
| **`CompositionMode_Multiply`** | Dark stays dark; light colour stains paper | **Primary highlighter** (classic marker) |
| **`CompositionMode_Darken`** | Result is min of source/dest channels | Alternate highlighter |
| **`CompositionMode_SourceOver` + low alpha** | Washes the whole area including text | Pen outlines, sticky chrome — **not** fill highlight |
| **`CompositionMode_Screen` / Lighten** | Lightens darks | Rarely useful for “marker on text” |
| **`CompositionMode_DestinationIn` / masks** | Cutouts | Advanced erase / mask layers later |

**Recommendation:** default highlighter fill = **Multiply** with a saturated
but mid-value colour (classic `#f6d32d` / green / pink), optional opacity
multiplier. Pen/ink default = **SourceOver** with configurable alpha and width.

Paint order (bottom → top):

1. Page / image content (existing pipeline)
2. Annotation **highlight fills** (Multiply)
3. Annotation **ink / shapes / underlines** (SourceOver)
4. Existing text-region debug / selection overlays (keep above or toggle)

Use a dedicated `QPainter` (or intermediate `QImage` + composition) in the same
place as current `drawForeground` / text overlay so OpenGL viewport rules stay
consistent (see existing comments in `imageview` about overlay trails).

### Colour presets

Yellow, green, cyan, pink, orange — plus custom. Match common reader palettes;
store as RGBA + blend enum, not only alpha.

---

## 4. Tool catalogue (proposed)

### Phase 1 — MVP (ship first)

| Tool | Interaction | Geometry | Blend |
|------|-------------|----------|-------|
| **Select** | Click/drag annotations | Handles for move/delete | — |
| **Text highlight** | Drag across text regions (or rubber-band intersecting regions) | Region ids or page-space quads from text layer | Multiply fill |
| **Freehand highlighter** | Stroke | Polyline / smoothed path, large width | Multiply |
| **Pen / ink** | Stroke | Polyline, pressure optional later | SourceOver |
| **Eraser** | Stroke or click | Delete stroke / whole object | — |
| **Colour + width** | Palette | Shared tool state | — |

### Phase 2

| Tool | Notes |
|------|-------|
| Underline / strikeout / squiggly | Text-snapped, stroke under/through baseline |
| Rectangle / ellipse | Drag rect; optional Multiply fill |
| Line / arrow | Two-point |
| Sticky note / free text | Point + panel text (UI heavier) |
| Undo/redo stack | Per session page |

### Out of scope for early versions

- PDF `/Annot` write-back into source files
- Multi-user review threads
- Stylus pressure curves (can add once ink path exists)
- Redaction (secure wipe — different product surface)

---

## 5. Geometry and identity (biltoo fit)

### Coordinate space

Store annotation geometry in **document page space** (same contract as
`TextRegion::bbox` / [OCR_COORDINATES.md](OCR_COORDINATES.md) and
[CONTENT_COORDINATES.md](CONTENT_COORDINATES.md)):

```
page  ──pageRectToImageRect(pageYUp)──►  source (unoriented full raster)
source ──ContentXform / SessionAppearance──►  display (crop-local)
display + item->offset() ──mapToScene──►  scene   (paint with view transform)
view  ──mapToScene → item local − offset──►  display ──mapDisplayRectToSource──► source
      ──imageRectToPageRect──► page   (input)
```

Rules:

- **Storage:** page space only; never bake crop/orient into points or quads.
- **`pageYUp` / `pageBounds`:** prefer live `PageTextLayer` on the controller;
  else `ThumtooCache::cachedPageTextLayer`; else pixel box of source size with
  `pageYUp = false` (plain images).
- **Source size:** unoriented full raster (`ThumtooCache::cachedSize` first) —
  never oriented `item->imageSize()` without undoing aspect swap
  (CONTENT_COORDINATES.md).
- **Paint:** scene space while `paintForeground` still has the view transform
  (same as `TextLayerController::paintSceneOverlays`). Do **not** draw
  `mapFromScene` view-pixel coordinates into that painter.
- **Input rubber (text tool):** view-space rect for hit-test only; committed
  geometry is still page-space quads from region bboxes.
- Key by **`SessionImageId`**, never path alone ([IDENTITY.md](../IDENTITY.md)).

### Text-snapped highlights

Reuse **`TextLayerController` / region indices**:

1. User selects regions (existing rubber-band / click) **or** a dedicated
   highlight tool that hits `regionIndexAtViewPos` / intersecting region rects
2. Persist `{ sessionId, regionIndices[] }` **or** baked page-space quads
   (quads survive text-layer rebuild; ids are cheaper and track OCR refresh if
   we re-resolve carefully)
3. **Recommendation:** store **page-space quads** derived at create time from
   current region bboxes, plus optional `source: Native|Ocr` and text snippet
   for search/export. Do not depend on region index stability across re-OCR.

### Freehand

Store paths as lists of page-space points (or display→page mapped on stroke
end). Simplify (RDP) on finish to keep project files small.

---

## 6. Data model (sketch)

```
AnnotationDocument (session- or project-scoped)
  pages: map<SessionImageId, AnnotationPage>

AnnotationPage
  objects: vector<AnnotationObject>

AnnotationObject
  id: stable uuid or monotonic id
  kind: HighlightQuad | HighlightStroke | InkStroke | ShapeRect | …
  blend: Multiply | SourceOver | …
  color: QColor
  width: qreal          // stroke tools
  pageYUp: bool         // must match layer at create, or always page space flag
  geometry: quads[] | points[] | rect
  meta: created, textSnippet?, author?
```

Persistence: extend **project file** / session sidecar (JSON or CBOR). v1 does
not mutate the PDF on disk.

---

## 7. UI placement

| Surface | Role |
|---------|------|
| **Tools toolbar** (left, with Select/Pan/Zoom) | Annotation tool mode (Highlight, Pen, Eraser, …) |
| **Main toolbar or floating strip** | Colour swatches, width, blend hint |
| **View menu / Panels** | Show/hide annotation layer |
| **Edit** | Undo annotation, clear page annotations |
| **Text panel** | Optional list of text highlights (jump to quad) |

Do **not** overload the existing “Show Text Regions” debug outline with
permanent highlighter chrome; keep debug separate from user markup.

Mode interaction:

- Annotation tools take mouse in Image mode similar to Crop tool
- Pan still available (middle mouse / space / Pan tool)
- Leaving annotation tool restores previous tool

---

## 8. Architecture sketch

```
AnnotationController          // input, tool state, undo
  └── AnnotationSession       // per-sid object lists
AnnotationPainter             // drawForeground collaborator
  └── blend + map page→display
TextLayerController           // hit-test for text-snapped create
ImageView                     // dispatch pointer events by tool
```

Paint: one collaborator next to text overlay in `drawForeground`, after image
items, before HUD. Highlight fills first (Multiply), then ink.

Undo: command stack of add/remove/edit object; do not piggyback on appearance
undo without a clear boundary.

---

## 9. Implementation phases

### Phase A — foundation (first implementation PR)

1. Data types + in-memory store keyed by `SessionImageId`
2. `AnnotationPainter` with Multiply rect demo (manual quads)
3. Tool: **text highlight** from intersecting text regions
4. Tool: **freehand highlighter** stroke
5. Show/hide + clear page + basic undo
6. Project save/load hook (if project format already has extension points)

### Phase B — ink and select

1. Pen tool (SourceOver)
2. Eraser / select-delete
3. Width and colour UI
4. Stroke simplification

### Phase C — shapes and export

1. Rect / ellipse / line
2. Underline from text baseline
3. Export flattened PNG/PDF (optional)
4. PDF /Annot write (large; separate project)

---

## 10. Risks and decisions

| Topic | Decision |
|-------|----------|
| PDF-native annotations | **Defer**; session overlay first |
| Highlight vs path fallback | Prefer text-snapped when layer exists; freehand always available |
| Re-OCR invalidates region ids | Store **quads**, not indices |
| Multiply blend | Software-raster viewport (default); Multiply via QPainter composition |
| Crop changes | Page-space storage → paint follows crop (same as OCR boxes) |
| Gallery mode | Annotations only in Image mode for v1 |

---

## 11. Acceptance criteria (MVP)

- [ ] Yellow (Multiply) highlight over black body text keeps glyphs readable
- [ ] Highlight can target text regions from native or OCR layer
- [ ] Freehand highlighter works on images without a text layer
- [ ] Annotations survive crop/rotate appearance changes (page space)
- [ ] Annotations key off `SessionImageId`
- [ ] Show/hide and clear without touching source files
- [ ] Undo last stroke/highlight

---

## 12. Decisions (locked 2026-09-28)

1. **Persistence:** embed in `.biltoo` project JSON (`annotations` array).
2. **Default tool:** freehand highlighter (Multiply).
3. **Sticky notes:** Phase C only.
4. **Input:** mouse only for Phase A/B; stylus/pressure later.

---

## 13. References (apps / specs)

- ISO 32000 text markup and ink annotation families (Highlight, Underline,
  Squiggly, StrikeOut, Ink, …)
- Okular annotation toolbar (text vs graphic annotations)
- Xournal++ pen / highlighter / PDF text toolbox
- Qt `QPainter::CompositionMode` (Multiply, Darken, SourceOver)
