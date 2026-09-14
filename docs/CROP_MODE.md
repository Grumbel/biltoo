# Crop mode — contract (Image, Gallery, Workspace)

One plan for all modes. Implementation must follow this; mode-specific code
only changes **view** (fit/pan), never content identity or geometry rules.

## Authority

| Data | Store | Key |
|------|--------|-----|
| Crop + orient + grade | `SessionAppearanceStore` (`m_appearance`) | `SessionImageId` |
| Pixels (unoriented host) | `ImageCache` | path |
| Live draft (crop UI only) | `m_cropRect` / `m_cropRotation` on `ImageView` | crop session |

Path-keyed XDG **never** stores crop for bound session images.

## Spaces (see CONTENT_COORDINATES.md)

- **cropRect** is in **post-orient full-frame** space (after flips + quarter-turns,
  before any crop bake).
- **cropSourceSize** is the size of that space when the crop was recorded
  (`layoutSize(fileNative, orientOnly)` — **not** soft pixels, **not** a prior
  crop box).
- Draft chrome maps `cropRect` into `item->contentRect()` via item offset.

## Enter crop (every mode)

Preconditions: single subject, `hasDisplayPixels()`.

1. Lock `m_cropTargetItem` / `m_cropTargetId` for the whole session.
2. Snapshot enter state for Cancel (appearance + pixels if available).
3. **Install orient-only full frame** (`installFullImageForCrop`):
   - Host = `ImageCache` unoriented preferred.
   - `contentOnly` = appearance with **crop cleared**.
   - `materializeDisplay(softOrFull, contentOnly)` then `attachDisplaySample`.
   - `applyContentLayoutSize(item, contentOnly)` → intrinsic = **full** orient size.
   - Clear item session crop flags. Applied xform = contentOnly.
   - **Never** leave a prior crop bake on the item. **Never** run incremental
     `bakeRotate90` after attach (double-transpose vs layoutSize).
4. Init draft: if store has crop, `scaleCropRect(prior, cropSourceSize, imageSize)`
   into content space; else full contentRect.
5. **View only (Image mode):** `fitInView` / transform so the full frame is
   visible. Must **not** change intrinsic using a want that still has crop.
   **`m_cropMode` must be true before any `fitItem` call** (Image mode runs
   fitItem at the end of prepare). If fitItem runs with m_cropMode false, it
   applies store crop to intrinsic and the draft collapses to the old crop bake.
6. Block `installDisplayPixels` / async rematerialize on the crop target until
   leave (draft must not be overwritten by a crop bake).

Gallery: does not host crop UI. Open the subject in Image mode, then enter.

## Apply crop

1. `recordSessionCrop`: write `cropRect` + `cropSourceSize` (oriented full size)
   + rotation under **crop target id** in `m_appearance`.
2. Materialize from unoriented host + full want (including crop). Soft ≤512 on GUI.
3. `attachDisplaySample` + `applyContentLayoutSize(item, want)` → intrinsic =
   **crop box** via `layoutSize(fileNative, want)`.
4. Placement scale: Workspace keeps enter-time scale; Image uses fit (view only).
5. Peers + filmstrip via `commitItemSessionEdit` / id-keyed signals.
6. Leave crop mode (clear draft flags). Async host upgrade may run **after** leave.

## Cancel / Reset

- Cancel: restore enter appearance + pixels path (or re-materialize from store
  as it was at enter).
- Reset: clear crop in store; show orient-only full frame.

## Repeated crop (second enter)

Same as first enter. Store still has crop → step 3 shows **full** frame; step 4
places prior rect. Draft is never the previous crop bake.

## Mode differences (allowed)

| | Workspace | Image | Gallery |
|--|-----------|-------|---------|
| Host crop UI | yes | yes | no → open Image |
| After enter | keep placement scale; centre | **view** fit only | n/a |
| After apply | keep placement scale | **view** fit only | n/a |
| Pack / scroll | scene rect | sceneRect tight | ContentChange pack; prefer scroll restore |

## Forbidden

- Soft sample size as intrinsic.
- `fitItem` / any helper rewriting intrinsic from `wantAppearance` **while
  crop mode is active** if that want still has crop (collapses draft to the
  old crop box — Image mode regression).
- Path-keyed crop for bound ids.
- Painting filmstrip from path decode over an id crop override.
- Async rematerialize onto the crop target during `m_cropMode`.

## Debug

`BILTOO_DEBUG_CROP=1`: enter-full must log `imageSize=<full>` `appliedCrop=0`.
