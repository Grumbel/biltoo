# Filmstrip layout contract

Single layout model for `ThumbnailBar` / `ThumbnailDelegate`. Implement against
this doc; do not invent parallel sizing rules in paint or decode paths.

## Role of `thumbSize`

`thumbSize` is a **logical pixel** length: the size of the thumbnail **along the
strip’s cross-axis** (the thin axis of the bar).

| Bar orientation | Cross-axis | Image edge that equals `thumbSize` |
|-----------------|------------|-------------------------------------|
| Horizontal      | height     | image **height** (letterbox) or both (crop) |
| Vertical        | width      | image **width** (letterbox) or both (crop) |

Resizing the bar changes `thumbSize` via `thumbSizeFromBarExtent` so the
cross-axis slot tracks the bar.

## Modes

### Crop-to-square (default)

- Source is center-cropped to a square, then scaled to `thumbSize × thumbSize`.
- Every cell is identical: `thumbSize + 2·pad` by `pad + thumbSize + pad + label`.
- The image fills the icon slot edge-to-edge.

### Letterbox (crop off)

- The whole image is kept; aspect is preserved.
- The image is scaled so its **cross-axis edge equals `thumbSize`**:
  - Horizontal bar → height = `thumbSize`, width = `thumbSize × (w/h)`
  - Vertical bar → width = `thumbSize`, height = `thumbSize × (h/w)`
- Landscape in a horizontal bar is therefore **wider** than `thumbSize` and
  still **fills the bar height** (plus the same pad as left/right).
- Portrait is narrower (horizontal bar) or shorter (vertical bar).
- Cell **hugs** that content: same `cellPad` on every side, optional label band
  under the image.
- By construction, every letterbox cell shares the same cross-axis outer size
  (`pad + thumbSize + pad + label`), so the strip does not top-align short
  cells or leave an empty band under landscape thumbs.

## Pad and label

- `cellPad()` = `clamp(1, thumbSize/20, 6)` — identical on all four sides.
- Label band (optional) sits under the image only; it does not change crop vs
  letterbox geometry.

## Logical layout vs decode pixels

**Layout never uses decode/ladder pixel dimensions as cell sizes.**

| Concern | Rule |
|---------|------|
| `ThumbContentSizeRole` | Logical content size at `thumbSize` scale (aspect only) |
| `sizeHint` / `cellSizeForContent` | From that logical size + pad + label |
| Decode / `prepareThumbnailFromImage` | May use a larger ladder edge for sharpness |
| Icon pixmap | May be device pixels; `setDevicePixelRatio` so logical size matches role |

If decode produced a 512px-tall raster for a 96px `thumbSize` slot, the cell is
still 96px tall on the cross-axis — not 512.

## Paint

- Selection/hover fill the **full cell** (pad + image + label).
- Crop: pixmap fills `iconRect` edge-to-edge.
- Letterbox: pixmap fills `iconRect` (aspect matches by layout); hairline on
  the image bounds.

## What not to do

- Do not set long-edge = `thumbSize` for letterbox (landscape stays short of the
  strip and needs empty bands or centering hacks).
- Do not feed ladder/decode pixel sizes into `sizeHint`.
- Do not force a square `gridSize` in letterbox mode.
