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
   - Cross-axis: full `cellPad` (**fixed** logical pixels, not aspect-relative).
   - Flow-axis: `flowPad` (fixed, half of `cellPad`) per side so inter-image gap ≈ `cellPad`.
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

## Decode edge (no soft-only clamp)

`filmstripDecodeEdge()` = `ceilLadderEdge(thumbSize × DPR)` on the power-of-two
ladder `{128, 256, 512, 1024, 2048}`. Soft durable levels (≤512) are a
**placeholder**; `ThumbDecodeEdgeRole` tracks installed long edge and the
scheduler upgrades until `have ≥ ~90%` of the display step.

`kMaxThumbSize` is **1024** (layout max, matches highest ladder step).

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

- `cellPad()`: cross-axis margin — **8** logical px (absolute). Top/bottom on a
  horizontal bar; left/right on vertical.
- `flowPad()`: **4** logical px (`cellPad/2`) on each flow-axis side of the cell.
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
2. **Scroll anchor** — capture the row under the **viewport centre**; after
   layout, keep that image centred (`PositionAtCenter` + pixel fine-tune).
3. **Decode debounce (120ms)** — sharper pixels only after the size stops
   changing. A full `invalidateThumbPixels` on every drag step cancelled
   in-flight loads and left holes until scroll.

### Load window

`scheduleVisibleThumbnailLoads` uses viewport samples + overscan (~2× visible
cells, minimum 16 rows). Concurrent pool jobs default to **24**
(`BILTOO_FILMSTRIP_THUMB_LOADS`, 1–64). Soft misses wait on `ladderReady` via
`m_thumbAwaitLadder` (not permanent fail).

## Session appearance (crop / flip / turns)

Filmstrip does **not** own content appearance. It only displays a derived thumb.

| Situation | Behaviour |
|-----------|-----------|
| Row has `SessionImageId` + id override | Paint override; **do not** path-decode over it |
| Row has `SessionImageId`, no override yet | Decode **raw** path pixels only (no path-keyed XDG bake) |
| No session ids on the strip (unbound) | Optional path-keyed Thumtoo XDG appearance as first-open hint |

### Rules

1. **`setSessionImageOverride(SessionImageId, path, image)`** is the only way
   bound crops/rotations reach the strip (from `sessionCropApplied` /
   `sessionAppearanceChanged` with id).
2. **`makeThumbnail(path)`** must not apply path-keyed XDG appearance when any
   row has a valid `SessionImageId` — that leaked one path’s crop onto every
   duplicate and overwrote id overrides after async climb.
3. Async job completion (including weak LQIP) must **skip install** when an
   id or path override already owns the cell; always clear `m_thumbLoadScheduled`
   on the GUI thread so the row is not stuck.
4. Path-only `setSessionImageOverride(path, image)` is legacy for unbound rows;
   ignored when the strip has session ids.

### Bug class avoided

Crop Apply paints correct thumb → ladder/job installs full-path decode → crop
“vanishes”. Root: path authority competing with id override.


## Decode edge vs LQIP / tiles

`filmstripDecodeEdge()` = `ceilLadderEdge(thumbSize × DPR)` floored at 128.

**Product (aligned with Gallery):** LQIP underlay + **tiles**. Soft PreferCache /
classic `loadThumbnail` encode is **removed** for filmstrip.

| Step | Action |
|------|--------|
| Size known | layout aspect from size memo/probe |
| LQIP in ImageCache | install underlay (cache-only; never `request_lqip`) |
| Durable tiles known | `scheduleDisplayPixels` → PreferCache **TileSynth** |
| Cold (no tiles) | worker `hasDurableTiles` rediscovery → TileSynth when known, else pyramid; surface tick re-arms every 1.5s |

`prepareThumbnailFromImage` **never upscales**. LQIP (≤96) must keep a small
`ThumbDecodeEdgeRole` so the quality watchdog continues until TileSynth covers
the strip edge. Upscaling LQIP used to mark cells settled while still blurry.


## Session appearance / crop overrides

Crop and orient bakes are keyed by **SessionImageId**, not path.

1. `sessionCropApplied` → `setSessionImageOverride(id, path, image, fromCropApply=true)`
   - stores override, sets **crop sticky**, installs icon, `viewport()->update()`
2. `sessionAppearanceChanged` → `fromCropApply=false`
   - **no-op** while sticky (must not replace crop with full-frame soft)
3. **Paint** uses `resolvedThumbPixmap(row)`: override first, else path thumb
4. `scheduleVisibleThumbnailLoads` applies overrides **before** the settled-edge skip
5. Never put filmstrip icons into `ImageCache` (poisoned host for Apply)

Override image on crop Apply must be the **crop bake** pixel size, not the full
frame (see CROP_MODE.md — clear FullSource before SoftPreview attach).
