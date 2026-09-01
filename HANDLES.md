# Workspace transform chrome — behaviour contract

This document is the normative description of how selection handles must behave
in Workspace mode. Implementation must satisfy it under **every** combination of
view zoom, item scale (including anisotropic and near-zero), rotation, and
HiDPI. If code and this document disagree, fix the code.

Related hard rules in AGENTS.md (“Workspace transform chrome”) remain in force.

## Goals (drawing-program analogy)

Chrome should feel like handles in Inkscape, GIMP transform tool, or a vector
editor:

- The selection **frame** is the visual boundary of the image (rotated parallelogram
  when the item is rotated). It moves, rotates, and scales with the image.
- **Scale handles** sit on the frame (corners + mid-edges). Their **positions**
  track the frame exactly (so they “align with the image” and rotate with it).
  Their **drawn size** is constant in viewport pixels; they never grow or shrink
  with item scale or view zoom.
- **Rotate handles** sit a fixed viewport-pixel distance outside the mid-edges
  (along the outward normal of the visual edge). Stems connect edge mid to knob.
  Knobs are constant screen size. Positions rotate with the image; distance does
  not scale with item scale.
- **Chrome buttons** (flip, raise/lower, reset) sit a fixed viewport-pixel
  distance outside the visual right edge of the frame. Same size and spacing
  in viewport pixels at all scales/rotations. Raise/Lower glyphs stay
  screen-upright; others may follow the item or stay upright as designed.
- **Opacity track** is a vertical slider outside the visual left edge. Bottom
  end is minimum opacity (5%), top end is fully opaque (100%). Placement is
  adaptive: prefer the lower part of the left edge when the frame is tall
  enough; always clear the left rotate knob and scale bar.
- Hit targets match painted centres (view-pixel distance tests). Input is owned
  exclusively by ImageView (viewport).

## Coordinate spaces

| Space | Used for |
|-------|----------|
| Item local | Pixmap rect, logical handle “attachment” points on the unscaled pixmap, crop |
| Scene | Item position + transform (scale/rotate/flip) |
| Viewport (logical pixels) | All painting of chrome, all hit distances, all fixed sizes (kHandleScreenPx etc.) |

**Never** stroke handle geometry under the item’s QTransform or the view’s
world transform for final size. Always:

1. Map attachment points with `view->mapFromScene(item->mapToScene(local))`.
2. Offset by constant vectors in viewport space when a fixed screen distance is
   required (rotate knobs, chrome stack).
3. Draw with cosmetic pens / explicit pixel sizes.

### Why local offsets of the form `kPx / axisScreenPerLocal` are fragile

Placing a point at local = edge + (kPx / sx) * axis keeps the **mapped**
distance ≈ kPx when the linear map is well-conditioned. When item scale → 0:

- sx → 0, local offset → ∞
- Intermediate `mapToScene` of huge local coordinates loses precision or produces
  Inf/NaN once the product scale * offset is no longer representable cleanly
- Frame corners collapse; edge direction vectors become zero; `norm(0)` returns
  an arbitrary unit vector → stems and bars point the wrong way or vanish

Therefore external handles (rotate, chrome buttons) **must** be placed by:

```
viewEdgeMid = map(localEdgeMid)
viewHandle  = viewEdgeMid + outwardNormal * kOffsetPx
```

with `outwardNormal` computed from the **mapped** parallelogram (or from the
mapped local axes). No division by a vanishing scale factor.

Scale handles stay at the mapped corners / edge mids (no extra offset).

## Degenerate / near-zero scale

- Define a practical minimum scale (e.g. 0.01 or a few screen pixels for the
  shorter side). Clamp in `setItemScale` / drag.
- When the mapped frame has diameter below a few viewport pixels, skip drawing
  scale/rotate chrome or draw a minimal centred indicator; do not feed zero
  vectors into `norm`.
- Edge and corner scale drags must not use ratios of local coordinates after the
  transform has already been updated in a way that amplifies noise. Prefer:

  - Fixed anchor in scene space
  - Desired half-extent = projected scene distance from anchor to pointer
    along the current scale axis (scene image of local X or Y)
  - `newScale = desiredHalf / localHalfExtent` (localHalfExtent from pixmap
    rect, constant)

  This stays stable as scale → 0 and when crossing zero (optional: allow flip
  through zero or clamp).

## Scale handle drag semantics

| Handle | Default (no modifier) | Ctrl or Shift |
|--------|------------------------|---------------|
| Corner | Uniform scale about item centre (distance ratio) | Uniform scale about opposite corner (anchor fixed in scene) |
| Edge   | Anisotropic scale about centre (stretch one axis) | Anisotropic scale about opposite edge (anchor fixed) |

Axes for edge stretch are the item’s local X/Y (image axes), not screen axes,
so a rotated image still stretches “width” and “height” of the picture.

## Paint order and stacking

Chrome for selected items is painted in ImageView::paintEvent after the scene,
ordered by stackZ so higher items’ handles appear above lower images. Frame,
then rotate stems/knobs, then scale brackets/bars, then chrome buttons, then
opacity track.

## Hit testing

- Prefer view-pixel distance to mapped centres / projected distance to edge
  segments.
- Edge scale targets are short segments centred on the mid-edge, thickness ~
  handle size, not the full edge (avoids stealing corner hits).
- View owns hover and press; do not rely on QGraphicsItem shape delivery for
  external handles.

## Validation checklist (manual)

1. Place an image, select it: frame + 4 corners + 4 edge bars + 4 rotate knobs +
   chrome stack all visible, constant size under view zoom in/out.
2. Rotate the item 30° / 90° / 180°: frame and scale handles rotate with the
   image; rotate stems stay perpendicular to edges; chrome distance constant.
3. Scale the item down until it is a few pixels: handles must not fly to
   infinity, vanish, or produce NaN; edge drag remains controllable; clamp at
   min scale.
4. Scale up large: handles stay the same pixel size; no thickening.
5. Anisotropic scale (edge drag): opposite edge/centre stays put when modifier
   held; no jump when releasing.
6. Near-zero edge drag: no oscillation, no sudden jumps to huge scale.
7. HiDPI: sizes still match design px (devicePixelRatio handled by Qt painter /
   mapFromScene).

## Implementation notes (current code map)

- Centres: `ImageItem::handleCenter` (local) — **candidate for rewrite** of
  external offsets to pure view-space placement.
- Paint: `ImageItem::paintInteractionChrome` (viewport) via `ImageView::paintEvent`
- Drag: `ImageItem::applyScaleHandleDrag`, `beginHandleInteraction` /
  `updateHandleInteraction`
- Hit: `handleAt`, `handleDistanceScreenPx`, edge segment loop
- Constants: `kHandleScreenPx`, `kRotateOffsetPx`, `kChromeBtnScreenPx`, …

When fixing, prefer small commits: (1) document + TODO, (2) stable placement of
external handles, (3) degenerate-frame paint guards, (4) stable scale drag math,
(5) any remaining hit/cursor polish.

