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
3. **Cell** = content + cross/flow pads (+ optional label band under the image).
   - Cross-axis: full `cellPad` (matches bar-edge margin).
   - Flow-axis: `flowPad` (= `cellPad/2`) per side so inter-image gap ≈ `cellPad`.
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

Every install path (pool worker, session override, ladder) must call
`prepareThumbnailFromImage(src, filmstripDecodeEdge())` before
`setThumbnailIcon`. No other maxSize.

- **Crop:** center-crop square → `scaled(max, max, KeepAspectRatio)`.
- **Letterbox:** `scaled(max, max, KeepAspectRatio)` (longest edge → max).
- **Never** `IgnoreAspectRatio`.
- `max` = `filmstripDecodeEdge()` (sharpness only, not layout size).

## Lifecycle

| Event | Action |
|-------|--------|
| `setFiles` | `ThumtooCache::cachedSize` aspect when known; else provisional square + `scheduleProbe`; `ThumbLoaded=false` |
| `sizeReady` | Updates letterbox sizeHint for unloaded rows (same supply path as Gallery) |
| decode done | `setThumbnailIcon`: pixmap, content, sizeHint, layout visible rows |
| `setThumbSize` | `refreshAllItemGeometry` or full `scheduleThumbnailLoads` if sharper needed |
| `setCropToSquare` | `scheduleThumbnailLoads` → `invalidateThumbPixels` + reload |
| `scheduleThumbnailLoads` | **Always** `invalidateThumbPixels` first |

## Pad / spacing

- `cellPad()`: cross-axis margin (`~thumbSize/24`, clamped). Top/bottom on a
  horizontal bar; left/right on vertical.
- `flowPad()`: `cellPad()/2` on each flow-axis side of the cell.
- Item spacing: `cellPad - 2·flowPad` (0 or 1). Together with the two flow pads,
  the empty gap between adjacent image contents equals `cellPad` — same as the
  cross-axis margin against the bar edge.
- Selection/hover fills the **full cell**.

## Consistency checks

1. Paint dest aspect == `logicalContentSize` aspect (KeepAspectRatio into inner).
2. sizeHint cell hugs that content + pad.
3. No path may attach a pixmap without updating `ThumbContentSizeRole` + sizeHint.
4. No path may leave `ThumbLoadedRole=true` after a mode/size invalidate without reload.

## Debug

Set `BILTOO_DEBUG_FILMSTRIP=1` to log each `setThumbnailIcon` (row, source size,
logical content, sizeHint, crop flag).

## Invariants (must always hold)

1. Horizontal: `sizeHint.width >= content.width + 2·flowPad`;
   `sizeHint.height >= content.height + 2·cellPad + label`
2. Vertical: `sizeHint.width >= content.width + 2·cellPad`;
   `sizeHint.height >= content.height + 2·flowPad + label`
3. Paint dest aspect equals `logicalContentSize` aspect (KeepAspectRatio)
4. `ThumbLoadedRole == true` iff `ThumbPixmapRole` holds a non-null pixmap
5. After `invalidateThumbPixels`, every row has `ThumbLoadedRole == false`
6. `iconSize` is never larger than needed for letterbox width (sizeHint owns width)


## Resize / scroll

Dragging the filmstrip dock/splitter changes `thumbSize` via `resizeEvent` →
`setThumbSize` on every pixel. Behaviour:

1. **Geometry immediately** — `applyThumbMetrics` + `refreshAllItemGeometry`
   (sizeHints / content roles at the new `thumbSize`).
2. **Scroll anchor** — capture the current (or first visible) row and its offset
   in the viewport; after layout, restore so the strip does not jump.
3. **Decode debounce (120ms)** — sharper pixels only after the size stops
   changing. A full `invalidateThumbPixels` on every drag step cancelled
   in-flight loads and left holes until scroll.

### Load window

`scheduleVisibleThumbnailLoads` uses viewport samples + overscan (~2× visible
cells, minimum 16 rows). Concurrent pool jobs default to **24**
(`BILTOO_FILMSTRIP_THUMB_LOADS`, 1–64). Soft misses wait on `ladderReady` via
`m_thumbAwaitLadder` (not permanent fail).
