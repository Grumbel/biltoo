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
3. **Install orient-only full frame** (`prepareCropModeFullImage`):
   - Host = `ImageCache` unoriented preferred.
   - `contentOnly` = appearance with **crop cleared**.
   - `materializeDisplay(sample, contentOnly)` then `attachDisplaySample`.
   - Identity orient may keep host resolution (cap 2048); orient bake on GUI
     stays ≤ `kGuiMaterializeMaxEdge` (512).
   - `applyContentLayoutSize(item, contentOnly)` → intrinsic = **full** orient size.
   - Clear item session crop flags. Applied xform = contentOnly.
   - **Never** leave a prior crop bake on the item. **Never** run incremental
     `bakeRotate90` after attach (double-transpose vs layoutSize).
4. Init draft: if store has crop, `scaleCropRect(prior, cropSourceSize, imageSize)`
   into content space; else full contentRect.
5. **Then** set `m_cropMode = true` and (Image) `fitItem`.
   - `m_cropMode` must be true **before** `fitItem` so layout uses orient-only
     full size (store still has crop).
   - Viewport updates held across prepare so chrome never paints one frame of
     crop-on-old-box.
6. **Sample freeze** (`m_cropDraftSampleFrozen` + `m_cropDraftPath`) is set when
   the subject is locked — **before** `m_cropMode` and before the first draft
   attach. All install / ladder / async rematerialize paths must key off the
   freeze (not `m_cropMode` alone). `m_cropMode` stays false until after the
   first draft attach so crop chrome does not paint on the old bake for a frame.
   Cleared when leaving crop (`leaveCropModeInternal`).

Gallery: does not host crop UI. Open the subject in Image mode, then enter
(`hasDisplayPixels` is enough — soft is OK).

## Apply crop

1. `recordSessionCrop`: write `cropRect` + `cropSourceSize` (oriented full size)
   + rotation under **crop target id** in `m_appearance`.
2. Materialize from unoriented host + full want (including crop). Soft ≤512 on GUI
   when multi-MP.
3. **`clearDecodedPixels()` then `attachDisplaySample`**. Enter may have left
   FullSource host; `setPreviewImage` **ignores** SoftPreview while `m_source`
   is set — without clear, pixels stay full-frame and the filmstrip gets an
   uncropped override (`img=1365x2048` on `cropApply=1`).
4. `applyContentLayoutSize(item, want)` → intrinsic = crop box via
   `layoutSize(fileNative, want)`. **Never** soft sample size as intrinsic.
5. Placement scale: Workspace keeps enter-time scale; Image uses fit (view only).
6. Emit filmstrip with the **materialize display bake** (not a stale
   `displayImage()`).
7. Peers: `clearDecodedPixels` then install bake (stash may still hold full
   `m_source`; `setPreviewImage` alone is a no-op).
8. Leave crop mode. Async host upgrade may run **after** leave.

## Cancel / Reset

- Cancel: restore enter appearance + pixels path.
- Reset: clear crop in store; show orient-only full frame.

## Repeated crop (second enter)

Same as first enter. Store still has crop → step 3 shows **full** frame; step 4
places prior rect. Draft is never the previous crop bake.

## Filmstrip

| Signal | `fromCropApply` | Behaviour |
|--------|-----------------|-----------|
| `sessionCropApplied` | true | Store override, set sticky, force icon + paint |
| `sessionAppearanceChanged` | false | Skipped while sticky (must not demote crop) |

Paint uses `resolvedThumbPixmap(row)` → **override first**, then path thumb.
Never upscale LQIP to decode edge (false settle). Overrides checked before
“settled” skip in `scheduleVisibleThumbnailLoads`.

## Mode differences (allowed)

| | Workspace | Image | Gallery |
|--|-----------|-------|---------|
| Host crop UI | yes | yes | no → open Image |
| After enter | keep placement scale | **view** fit only | n/a |
| After apply | keep placement scale | **view** fit only | n/a |

## Forbidden

- Soft sample size as intrinsic (`attachDisplaySample` must use `layoutSize`).
- `fitItem` rewriting intrinsic from a **cropped** want while crop draft is
  active (or before `m_cropMode` is set).
- **`fitItem` during Apply while `m_cropMode` is still true:** if cropDraft is
  keyed only on `m_cropMode`, Apply’s fit forces orient-only full intrinsic on
  top of the new crop bake → stretch into the pre-crop contentRect. Draft is
  only when `m_cropMode` **and** no applied/session crop on the item.
- Soft crop attach without clearing FullSource first.
- Path-keyed crop for bound ids.
- Path decode painted over an id crop override.
- Async rematerialize onto the crop target during `m_cropMode`.
- Upscaling LQIP so `ThumbDecodeEdgeRole` reports the target edge.

## Slideshow

Slideshow phase buffers must use the **same** `SessionAppearanceStore` want as
Image/Gallery — including **crop**. `orientSlideshowImage` and phase upgrades
call `materializeDisplay(raw, app)` with the full appearance (never strip crop).

Identity is resolved by `sessionIdForPath` → `m_appearance.get(sid)`.

## Debug

```bash
BILTOO_DEBUG_CROP=1 BILTOO_DEBUG_FILMSTRIP=1 biltoo-run
```

Enter: `enter-full done imageSize=<full> appliedCrop=0`.  
Apply filmstrip: `cropApply=1 img=<cropW>x<cropH>` (not full-frame size).

## Tests

- `tests/contentxform_test.cpp` — `layoutSize` crop box, scale footprint,
  `needsRematerialize` crop→full.
- `tests/sessionappearance_crop_test.cpp` — `scaleCropRect`, materialize crop
  output size vs full frame.

## Source layout (ImageView crop TUs)

After tips **1573–1578**, single-use ImageView crop helpers were folded into the
pipeline entry points below. Prefer those names when reading or extending crop.

| File | Responsibility | Main entry points |
|------|----------------|-------------------|
| `imageview_crop.cpp` | Targets, draft locks, PathRaster cancel, shared layout/align, auto-trim | `cropTargetItem`, `fitImageOrUpdateWorkspace`, `relayoutAfterCropLeave`, `applyAutoCrop` |
| `imageview_crop_enter.cpp` | Enter session, full-frame draft install, mode toggle | `enterCropModeFromUi`, `setCropMode`, `prepareCropModeFullImage` |
| `imageview_crop_apply.cpp` | Apply bake/record, leave, undo push | `applyCrop`, `cancelCrop`, `applyCropCommit`, `leaveCropModeInternal`, `recordSessionCrop` |
| `imageview_crop_raster.cpp` | Full raster (thumtoo / pool) while draft shows provisional | `requestCropFullRaster`, `maybeUpgradeCropFullRaster`, `onPoolCropFullRasterDecoded` |
| `imageview_crop_paint.cpp` | Overlay chrome paint | `paintCropOverlay` |
| `imageview_crop_input.cpp` | Handle drag, rubber-band, hit-test, view mapping | `beginCropHandleDrag`, `beginCropRubberBand`, `cropHandleAt`, `cropPolygonView` |
| `imageview_appearance.cpp` | Session appearance load/store/restore / Apply-undo | `applyCropAppearance`, `applyStoredAppearance`, `restoreSessionCropAppearance`, `emitCropApplyAppearance`, `loadRestoreCropAppearance` |
| `cropsession.*` / `cropgeometry.*` | Pure policy and geometry (no ImageView state) | |
| `cropappearancecommand.*` | QUndoCommand for Apply | |
| `croppathraster.h` / `cropflash.h` / `cropdebug.h` | PathRaster suspend, HUD copy, debug logs | |

**Call sketch**

- Enter: `setCropMode(true)` → `enterCropModeFromUi` → `prepareCropModeFullImage`
- Apply: `applyCrop` → `leaveCropModeInternal(true)` → `applyCropCommit`
- Cancel: `cancelCrop` / `leaveCropModeInternal(false)` → `restoreSessionCropAppearance` when showing full
- Leave always clears draft freeze, PathRaster suspend, and tile LOD suppress inside `leaveCropModeInternal` (no separate `clearCropModeState`)

