# OCR and content coordinates

## Rule

**All OCR (and native) text region boxes are stored in document page space.**

`TextRegion::bbox` and `PageTextLayer::pageBounds` never depend on the session
crop, content orient, or colour grade. Those are **view transforms**.

Paint maps page → display every frame via `TextLayerController::regionImageRect`
(page → source → orient → crop-local). Changing or resetting the crop does **not**
require rewriting OCR boxes.

## Recognition vs storage

| Step | Space |
|------|--------|
| Tesseract on full-page URI raster | page (thumtoo maps pixels → page_bounds) |
| Tesseract on `materializeDisplay` bitmap | temporary display pixels |
| After install | **page only** (inverse ContentXform + `imageRectToPageRect`) |

When orient/crop/grade is active, biltoo OCRs the displayed bitmap so recognition
matches contrast and upright text, then **immediately** converts boxes to page
space. The session layer is therefore stable under later crop edits.

## Free-rotated crop

Forward and inverse maps use the axis-aligned bounding box of the four mapped
corners. Region quads are not rotated in the data model yet; a tilted crop
window can fatten boxes. Axis-aligned crops and 90° content turns are exact.

## Cache

thumtoo’s OCR store is keyed by document page (and optional engine/model), not
by session crop. Prefer full-page OCR in the store; session crop is paint-only.

## Source size basis

Appearance OCR scales the decode to `ThumtooCache::cachedSize(path)` when it
differs from the decoder’s pixel size so ContentXform and `regionImageRect`
share one native size. Mismatched sizes were a common source of box drift
after crop.
