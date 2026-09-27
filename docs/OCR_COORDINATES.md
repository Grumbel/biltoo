<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# OCR and content coordinates

Normative companion to [CONTENT_COORDINATES.md](CONTENT_COORDINATES.md) and
thumtoo `docs/PAGE_SPACE.md`.

## Rule

**All OCR (and native) text region boxes are stored in document page space.**

`TextRegion::bbox` and `PageTextLayer::pageBounds` never depend on the session
crop, content orient, or colour grade. Those are **view transforms**.

Paint maps page → display every frame via `TextLayerController::regionImageRect`
(page → source → orient → crop-local). Changing or resetting the crop does **not**
require rewriting OCR boxes.

## Page Y axis (`pageYUp`)

Every `PageTextLayer` carries:

| Field | Meaning |
|-------|---------|
| `pageBounds` | Axis-aligned page box |
| `pageYUp` | `true` → origin lower-left, Y up; `false` → origin top-left, Y down |

| Kind | `pageYUp` |
|------|-----------|
| PDF / DjVu / EPUB (native **and** OCR) | `true` |
| Plain-image OCR | `false` (`pageBounds` = source pixel box) |

Always read `layer.pageYUp` when mapping. Fallback without a layer:
`ThumtooCache::pageSpaceYUpForPath(path)` (document page refs → true).

Do not use `isDjvuFile`-only or path-string `"epub"` heuristics.

## Recognition vs storage

| Step | Space |
|------|--------|
| Tesseract on full-page URI raster | page (thumtoo maps pixels → page_bounds, Y-up for documents) |
| Tesseract on `materializeDisplay` bitmap | temporary display pixels (`pageYUp = false` on the RGB result) |
| After install | **page only** (inverse ContentXform + `imageRectToPageRect`) |

When orient/crop/grade is active, biltoo OCRs the displayed bitmap so recognition
matches contrast and upright text, then **immediately** converts boxes to page
space. The session layer is therefore stable under later crop edits.

thumtoo’s OCR store is keyed by document page (and optional engine/model), **not**
by session crop. Full-page URI OCR does not pass session crop into thumtoo;
session crop is paint-only.

## Spaces in the mapping pipeline

```
page  ──pageRectToImageRect(pageYUp)──►  source (full unoriented raster)
source ──ContentXform / SessionAppearance──► oriented → display
display ──mapDisplayRectToSource──► source ──imageRectToPageRect──► page
```

`pageRectToImageRect` / `imageRectToPageRect` only handle the page↔source Y axis.
Crop, flip, and quarter-turns are ContentXform’s job.

## Source size basis

Appearance OCR scales the decode to `ThumtooCache::cachedSize(path)` when it
differs from the decoder’s pixel size so ContentXform and `regionImageRect`
share one native size. Mismatched sizes were a common source of box drift
after crop.

## Appearance OCR (on-screen pixels)

1. Scale decode to `cachedSize` when needed.
2. `SessionAppearance::materializeDisplay` → `runOcrRgbImage`.
3. RGB OCR returns boxes in buffer space (`pageYUp = false`).
4. Host remaps display → source → page with `pageSpaceYUpForPath` and sets
   `layer.pageYUp`.
5. Install in-memory (`installLayer`). Appearance OCR is not required to write
   the thumtoo OCR store (appearance is session-local); full-page URI OCR does.

## Overlay paint / hit-test

`TextLayerController::regionImageRect`:

1. `pageRectToImageRect(..., pageYUp())` → source  
2. `SessionAppearance::mapSourceRectToContentDisplay` → display  

`pageYUp()` prefers `layer.pageYUp` when bounds are valid.

## Free-rotated crop

Forward and inverse maps use the axis-aligned bounding box of the four mapped
corners. Region quads are not rotated in the data model yet; a tilted crop
window can fatten boxes. Axis-aligned crops and 90° content turns are exact.

## Checklist

1. Mapping a region? Use **that layer’s** `pageYUp`.
2. No layer yet? `pageSpaceYUpForPath`.
3. Store OCR boxes in page space only — never bake session crop into the store.
4. Appearance OCR: remap before install; share `cachedSize` with paint.
