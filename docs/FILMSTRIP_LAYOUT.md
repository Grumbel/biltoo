# Filmstrip layout contract

Single model for `ThumbnailBar` / `ThumbnailDelegate`. Every path (sizeHint,
paint, prepare, crop toggle, thumbSize change) must follow this document.

## `thumbSize`

Logical pixels along the strip **cross-axis** (thin axis of the bar).

| Orientation | Cross-axis | Letterbox edge = `thumbSize` |
|-------------|------------|------------------------------|
| Horizontal  | height     | image **height**             |
| Vertical    | width      | image **width**              |

`thumbSizeFromBarExtent` updates `thumbSize` when the bar is resized.

## Modes

### Letterbox (default)

1. Keep full image aspect.
2. **Logical content** = `letterboxContentSize(aspect)`:
   - horizontal: `(thumbSize * w/h, thumbSize)`
   - vertical: `(thumbSize, thumbSize * h/w)`
3. **Cell** = content + `2·cellPad` (+ optional label band under the image).
4. Paint **centers** content in the padded inner box (fills when sizeHint matches).

### Crop-to-square (opt-in)

1. Center-crop source to square, scale to decode edge.
2. Logical content = `(thumbSize, thumbSize)`.
3. Uniform cells: `cellSize(font)`.

## Roles (per item)

| Role | Meaning |
|------|---------|
| `ThumbContentSizeRole` | Logical content size at current `thumbSize` (layout + paint) |
| `ThumbPixmapRole` | Prepared pixmap (decode-edge pixels, correct aspect) |
| `ThumbLoadedRole` | Real thumb installed (not placeholder) |

`logicalContentSize(index)` is the **only** way paint and sizeHint read content size.

## Prepare (`prepareThumbnailFromImage`)

- **Crop:** center-crop square → `scaled(max, max, KeepAspectRatio)`.
- **Letterbox:** `scaled(max, max, KeepAspectRatio)` (longest edge → max).
- **Never** `IgnoreAspectRatio`.
- `max` = `filmstripDecodeEdge()` (sharpness only, not layout size).

## Lifecycle

| Event | Action |
|-------|--------|
| `setFiles` | Provisional square content + sizeHint; `ThumbLoaded=false` |
| decode done | `setThumbnailIcon`: pixmap, content, sizeHint, layout visible rows |
| `setThumbSize` | `refreshAllItemGeometry` or full `scheduleThumbnailLoads` if sharper needed |
| `setCropToSquare` | `scheduleThumbnailLoads` → `invalidateThumbPixels` + reload |
| `scheduleThumbnailLoads` | **Always** `invalidateThumbPixels` first |

## Pad / spacing

- `cellPad()`: absolute filmstrip px (`~thumbSize/24`, clamped). Same every cell.
- Cell spacing: fixed 2px between items.
- Selection/hover fills the **full cell**.

## Consistency checks

1. Paint dest aspect == `logicalContentSize` aspect (KeepAspectRatio into inner).
2. sizeHint cell hugs that content + pad.
3. No path may attach a pixmap without updating `ThumbContentSizeRole` + sizeHint.
4. No path may leave `ThumbLoadedRole=true` after a mode/size invalidate without reload.
