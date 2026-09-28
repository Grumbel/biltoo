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

User **annotation** geometry follows the same storage rule — see
[ANNOTATION_OVERLAY.md](ANNOTATION_OVERLAY.md) §5.

## Page Y axis (`pageYUp`)

Every `PageTextLayer` carries:

| Field | Meaning |
|-------|---------|
| `pageBounds` | Axis-aligned page box |
| `pageYUp` | `true` → origin lower-left, Y up; `false` → origin top-left, Y down |

| Kind | `pageYUp` |
|------|-----------|
| PDF / EPUB via MuPDF (native **and** OCR) | `false` (MuPDF page space is top-left Y-down) |
| DjVu (native **and** OCR) | `true` (bottom-left Y-up) |
| Plain-image OCR | `false` (`pageBounds` = source pixel box) |

Always read `layer.pageYUp` when mapping. Fallback without a layer:
`ThumtooCache::pageSpaceYUpForPath` — **only DjVu → true**; PDF/EPUB/plain → false.

Cached text layers written with the old PDF=`true` flag need re-extract
(invalidate thumtoo text-layer blobs or re-open after cache clear).

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
2. `ContentXform::mapSourceRectToDisplay` via **`wantAppearanceForItem`**
   (session + contentBake + applied fingerprint — same as display install).
   Crop draft is orient-only full frame even when the store still has a crop.
3. If `item->imageSize()` ≠ `layoutSize`, scale so boxes track the painted contentRect  

`pageYUp()` prefers `layer.pageYUp` when bounds are valid. Region bboxes stay
in page space; paint always follows the applied content transform.

## Free-rotated crop

Forward and inverse maps use the axis-aligned bounding box of the four mapped
corners. Region quads are not rotated in the data model yet; a tilted crop
window can fatten boxes. Axis-aligned crops and 90° content turns are exact.

## Checklist

1. Mapping a region? Use **that layer’s** `pageYUp`.
2. No layer yet? `pageSpaceYUpForPath`.
3. Store OCR boxes in page space only — never bake session crop into the store.
4. Appearance OCR: remap before install; share `cachedSize` with paint.

## Source DPI (Tesseract)

Tesseract needs a realistic source resolution. Without it (default ~70 DPI),
pages with mixed body/caption/header sizes segment poorly — especially when
OCR runs on a **cropped** appearance bitmap (the buffer looks like a tiny
physical scrap).

| Path | DPI used |
|------|----------|
| Full-page URI OCR | `72 × raster_w / page_bounds_w` (points), clamped 70–600 |
| Appearance OCR (document) | same, from `cachedSize` × native `pageBounds` |
| Appearance OCR (plain image) | **300** |
| Host override | `OcrOptions::dpi` / `runOcrRgbImage(..., sourceDpi)` |

Crop does not change the correct DPI: character height in pixels is set by the
native raster scale, not by the crop window size.

## Appearance OCR pixel buffer

`runOcrRgbImage` always packs a **dense RGB888** buffer (`width × height × 3`,
no row padding) by expanding `Format_ARGB32` pixels component-wise. Graded
frames from `applyColorAdjustments` are ARGB32; feeding padded or ARGB memory
to thumtoo as if it were contiguous RGB888 produces skewed / garbage OCR.

## Reading order

Tesseract `ResultIterator` at `RIL_TEXTLINE` already yields reading order.
Biltoo keeps that sequence for OCR layers (`preferSourceOrder`). Geometric
re-sort is only for native extract; when used, **Y-up page space** compares
visual top via `QRectF::bottom()` (not `top()`), otherwise multi-line text
reads bottom-to-top.
