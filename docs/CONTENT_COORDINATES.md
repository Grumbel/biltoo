# Content appearance coordinate spaces

Text regions, crop chrome, and content orient (flip / 90° turns) share one
pipeline. Getting the **space** wrong is the usual overlay bug — not the paint
API.

## Spaces

| Name | Origin | Axes | Who produces it |
|------|--------|------|-----------------|
| **page** | page box from the document | may be Y-up (PDF/DjVu) or Y-down (EPUB) | thumtoo text layer |
| **source** | top-left of the full unoriented page raster | X right, Y down | decode / `cachedSize` |
| **oriented** | top-left after content flip + quarter-turns | X right, Y down | `bakeFlip` / `bakeRotate90` |
| **display** | top-left of what `ImageItem` paints | X right, Y down | oriented, then crop-local if `hasCrop` |

`pageRectToImageRect` maps **page → source** (handles Y-up).

`mapSourceRectToContentDisplay` maps **source → display**:

1. horizontal / vertical **content flips** about the full source size  
2. **quarter-turns** as repeated **+90° clockwise** steps in Y-down image coords  
   (same visual as `QTransform::rotate(+90)` + `QImage::transformed`)  
3. **crop**: intersect with `cropRect` (post-bake space), translate so crop
   top-left is `(0,0)`

Paint then does `displayRect.translated(item->offset())` and `mapToScene`.

## Live vs reload order

| Path | Order |
|------|--------|
| User flips / rotates now | bake into current pixels; `mapCropThrough*` keeps crop in post-bake space |
| User crops now | `cropToLocalRect` on current (already oriented) pixels; `cropRect` stored in that space |
| `applyContentToItem` reload | **crop then orient** on a fresh decode (historical); overlays still use flip → turn → crop because stored `cropRect` is post-bake |

Do not mix “crop in source space” with “crop in oriented space” without
`mapCropThrough*`.

## Rotation sign

- `contentQuarterTurns` is absolute `0..3` (CW steps from identity).  
- `bakeItemRotate90(item, +1)` = CW; `bakeItemRotate90(item, -1)` = CCW.  
- Overlay mapping always applies the **absolute** turn count as CW steps — never
  a delta.

Qt note: `QTransform::rotate(+90)` rotates the coordinate system
counter-clockwise; with Y-down that yields a **clockwise** pixel rotation.
Point map used in code: `(x, y) → (H − y, x)`.

## Recovering source size for text

Text is authored against the **native page raster**. Never take
`item->imageSize()` after orient/crop as the page→source basis.

Prefer, in order:

1. `ThumtooCache::cachedSize(path)`  
2. `m_imageSizeByPath`  
3. invert: start from live size, un-transpose if odd turns and no crop  

Then map, then **scale** the result into the actual `sourceImage()` /
`previewImage()` pixel size when magnitude differs (soft ladder).

## Checklist for overlay bugs

1. Which space is the input rect in (page / source / oriented / display)?  
2. Is `sourceSize` the unoriented full page, or already swapped by turns?  
3. Is `cropRect` post-bake (after `mapCropThrough*`) or raw?  
4. Does the live item show full or soft pixels (scale magnitude)?  
