# Resolution selection state machine

Normative install policy lives in `DisplaySurface::decide`
([DISPLAY_SURFACE.md](DISPLAY_SURFACE.md)). Host climb lives in
`PathRasterService` / `RasterClimb` ([PATH_RASTER_SERVICE.md](PATH_RASTER_SERVICE.md)).
Pixel bake (crop, free rotation, flips, grade) lives in
`SessionAppearance::materializeDisplay` ([CONTENT_PIPELINE.md](CONTENT_PIPELINE.md)).

This document maps the **full** resolution path, including **crop** and
**crop rotation**, matches it to code, and states what “optimal for window
size” means (and where it does not hold).

Tip context: crop-aware FullSource settle is **biltoo-979**; ImageFocus need
includes file native long edge as of **biltoo-978**.

---

## 1. Layering

```text
┌─────────────────────────────────────────────────────────────┐
│ 1. DisplaySurface::decide(State)     pure install policy    │
│    inputs: need, haveDisplay, attachedKind, applied, want,  │
│            frozen, climbPending, hostLongEdge               │
│    outputs: None | AttachSoft | AttachFull |                │
│             ScheduleClimb | ScheduleAsyncMaterialize        │
└───────────────────────────┬─────────────────────────────────┘
                            │ ImageView::applyDisplaySurfaceAction
          ┌─────────────────┼──────────────────┐
          ▼                 ▼                  ▼
   PathRasterService   materialize on GUI   worker materialize
   ensure(path,need)   AttachSoft/Full      ScheduleAsync…
          │
          ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. RasterClimb::Machine (+ PathRasterService)               │
│    Soft → PreferCache (≤~1024) → Full (native)              │
│    host samples only → ImageCache (unoriented, upward-only) │
└───────────────────────────┬─────────────────────────────────┘
                            │ host improved / delivery
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. SessionAppearance::materializeDisplay(host, want, kind)  │
│    flips → quarter turns → crop (+ free rotation) → grade   │
│    output: post-crop display pixels → item attach           │
└─────────────────────────────────────────────────────────────┘
```

ImageView reports **need** and **applies** actions. It must not invent a second
InstallHostBetter loop (removed with the quality watchdog).

---

## 2. State vector (`DisplaySurface::State`)

| Field | Meaning | Crop note |
|-------|---------|-----------|
| `needEdge` | Target display long edge for this surface | ImageFocus: max(on-screen, **file** native), ladder-capped |
| `haveDisplayEdge` | Long edge of pixels on the item | **Post-crop** after materialize |
| `attachedKind` | `None` / `SoftPreview` / `FullSource` | FullSource ⇔ `ImageItem::hasDecodedPixels()` |
| `applied` / `want` | `ContentXform::Value` | `hasCrop`, `cropRect`, `cropSourceSize`, **`cropRotation`**, flips, quarter turns, grade |
| `frozen` | Crop **draft** hold | `decide` → always `None` |
| `climbPending` | PathRaster still working this path | Suppress duplicate `ScheduleClimb` |
| `hostLongEdge` | `ImageCache` sample long edge | **Pre-crop** file space |

Built by `ImageView::displaySurfaceStateForItem` (`src/imageview_load.cpp`).

`ContentXform::equal` includes `cropRotation` (epsilon via `qFuzzyCompare` on
the stored angle). Changing free-rotation invalidates `applied == want` and
forces a rematerialize cycle.

---

## 3. Need calculation (“window size”)

### ImageFocus (Image mode)

```text
need = cappedDisplayEdgeForPath(path, itemOnScreenNeedEdge(item))
       // contentSceneRect → view × DPR → ceil ladder; clamp to known native
```

- After crop **Apply**, `contentSceneRect` follows the **crop intrinsic**, so
  on-screen need tracks the cropped footprint in the viewport.
- Need is **window-driven**, not file-native. Tip 978 forced `need ≥ file native`,
  which scheduled Full immediately and skipped Soft→Prefer paints. Zoom / 1:1
  raises on-screen need; climb then escalates (including Full when need > Prefer
  plateau).

### Gallery / Workspace

- Gallery: `galleryDisplayEdgeForItem` → cell on-screen need (soft band when
  not allowing high-res).
- Workspace: on-screen footprint only (no native floor).

### Adequacy

`coversNeed(have, need)` ≡ `have >= need * 9/10` (same ratio as
`RasterClimb::covers` / ImageView `coversEdge`). “Meets window” means within
that band, not pixel-identical.

---

## 4. `DisplaySurface::decide` (display install SM)

Code: `src/display/displaysurface.cpp`. Spec: [DISPLAY_SURFACE.md](DISPLAY_SURFACE.md) §4.1.

### 4.1 Frozen (crop draft)

```text
frozen → None
```

Draft attaches orient-only full frame via explicit crop APIs; store crop must
not drive layout or replace the draft sample (`CROP_MODE.md`).

### 4.2 FullSource && applied == want

```text
haveDisplay covers need?     → None          // settled for current need
est = estimatedDisplayLongEdge(host, want)
est > haveDisplay?
  host > GUI max (512)?      → ScheduleAsyncMaterialize
  else                       → AttachFull
host does not cover need?    → ScheduleClimb (unless climbPending)
else                         → None          // best bake from this host
```

**Never** treat raw `hostLongEdge ≫ haveDisplayEdge` as an upgrade by itself
(pre-crop vs post-crop). That comparison caused the 1s soft↔full pulse and
false “settled” when a Soft-sourced crop bake was marked FullSource.

### 4.3 SoftPreview && applied == want

```text
no host → ScheduleClimb if need unmet (else None)
host > 512:
  need unmet && !hostCoversNeed → ScheduleClimb
  else                          → ScheduleAsyncMaterialize
est(host, want) > haveDisplay   → AttachFull   // host ≤ 512 only reaches here
need unmet && !hostCoversNeed   → ScheduleClimb
else                            → None
```

### 4.4 Blank or applied ≠ want (crop Apply, content rotate, grade, …)

```text
no host                         → ScheduleClimb
host > 512 && no pixels yet     → AttachSoft once, then re-evaluate
host > 512                      → ScheduleAsyncMaterialize
host ≤ 512                      → AttachFull
```

### 4.5 Projected post-crop edge

`ContentXform::estimatedDisplayLongEdge(hostLongEdge, want)`
(`src/contentxform.cpp`):

```text
no crop / empty rect  → hostLongEdge
has crop              → host * cropLong / cropSourceLong
                        clamped to [1, hostLongEdge]
cropRotation          → still uses axis-aligned cropRect size in post-orient
                        space (same box materialize samples for free-rot)
```

`needsRematerialize` uses the same estimate when xform already matches.

---

## 5. Executor

`ImageView::applyDisplaySurfaceAction` (`src/imageview_load.cpp`):

| Action | Effect |
|--------|--------|
| `None` | no-op |
| `ScheduleClimb` | `PathRasterService::ensure(path, climbNeed, logicalSize, policy)` |
| `ScheduleAsyncMaterialize` | worker `materializeDisplay` → `finishAsyncHostRematerialize` |
| `AttachSoft` / `AttachFull` | GUI install → materialize (GUI content max 512) |

After **AttachSoft**, the surface is synced and **decide is run again** so Soft
can become `ScheduleAsyncMaterialize` without waiting for another external
event.

`canAcceptDisplaySample` consults `decide` so attach entry points stay aligned.

---

## 6. Host climb SM

Path-keyed (not SessionImageId / crop-keyed):

```text
ensure(want) → Soft (≤512) → PreferCache / overview (≤~1024) → Full (if need > Prefer)
have ← deliveries into ImageCache only (upward-only)
PreferCache plateau is normal; raising want clears the plateau latch
EscalateToFull (ImageFocus / slideshow): Full only **after** Prefer plateau
  (same-plan Soft+Prefer+Full starved intermediate UI updates)
SoftDisplay (filmstrip / Workspace non-focus / Slideshow): soft band, no Full
```

Crop does **not** change the host want formula: decode enough of the **full
file** so the crop region has samples after materialize. Edit-time native
(`fullRasterForEdit` / crop full raster) is separate from display climb
([PATH_RASTER_SERVICE.md](PATH_RASTER_SERVICE.md) “Edit / crop full raster”).

---

## 7. Pixel bake (crop + crop rotation)

`SessionAppearance::materializeDisplay` order:

```text
host (unoriented)
  → content H/V flip
  → content ±90° (quarter turns)
  → crop in post-orient space
       axis-aligned: copy scaled cropRect
       free rotation (|cropRotation| > ε): rotated window sample
  → colour grade
  → display QImage  →  haveDisplayEdge
```

Content ±90° updates crop geometry via `mapCropThroughContentRotate90` so
`cropRect` / `cropSourceSize` / `cropRotation` stay in post-orient space.
Axis-aligned crops keep `cropRotation == 0` so free-rot is not stacked on turns.

---

## 8. Crop draft vs applied crop

```text
Enter crop
  m_cropDraftSampleFrozen = true → surface frozen → decide = None
  orient-only full-frame draft; store crop ignored for layout
  PathRaster / async rematerialize blocked for locked path

Apply
  write want (cropRect, cropSourceSize, cropRotation, …)
  soft stand-in if multi-MP; queue async full while freeze may still be on
  clear freeze → unfreeze surface
  applied ≠ want (or first bake) → rematerialize path
  finishAsync → FullSource, applied = want

Cancel
  restore prior appearance; unfreeze; no new crop in store
```

Resolution selection does not advance during draft (product rule).

---

## 9. Code ↔ spec checklist

| Spec | Code |
|------|------|
| frozen → None | `decide` first branch |
| FullSource + match + display covers need → None | `!needUnmet` |
| host projects better post-crop → AttachFull / Async | `estimatedDisplayLongEdge` |
| host short of need → ScheduleClimb | `!hostCoversNeed` |
| Soft match → Async / AttachFull; no Soft “upgrade” loop | Soft branch |
| want changed → soft once + async full | blank / mismatch branch |
| never demote matching FullSource to Soft | no Soft action in FullSource branch |
| climb only via PathRaster | `ScheduleClimb` → `ensure` only |
| materialize only via SessionAppearance | install + async paths |

---

## 10. Optimal resolution for window size

**Definition (ImageFocus):** post-crop `haveDisplayEdge` covers on-screen need
(9/10), and either the host cannot improve the projected post-crop edge further
or the host already covers the (capped) need / native floor.

### Converging scenarios

1. **Uncropped, steady view** — climb until host covers need; attach; settle.
2. **Crop Apply from Soft host, then native host** — est(native) > have →
   Async materialize → settle (979).
3. **Crop or cropRotation change** — want mismatch → one rematerialize cycle
   from best host, then same as (2).
4. **Zoom / resize** — need ↑ → climb and/or rematerialize until covered.

### Limits (not total correctness)

| Limit | Effect |
|-------|--------|
| Ladder quantization | Deliveries and need snap to ladder steps |
| PreferCache plateau | Temporary sub-native host is expected |
| ImageFocus need = on-screen only | 1:1 on a 6k file still climbs Soft→Prefer→Full; no native floor at fit |
| Linear crop estimate | AABB ratio; free-rot usable detail can differ slightly |
| Frozen draft | No progress until Apply/Cancel |
| Terminal PathRaster give-up | May stop short of native |
| Gallery LQIP+tiles | Placeholder LQIP; sharpness from tile pyramid |

**Monotonicity:** ImageCache host edge only moves up. Display edge moves up when
materialize runs from a better host. There is no legal cycle that permanently
re-attaches Soft over matching FullSource, and (after 979) no permanent settle
on a Soft-sourced crop bake while a better host is in cache.

**Conclusion:** eventual optimality holds for ImageFocus under the definition
above, modulo ladder / PreferCache / native-floor / failed Full — not as a
closed-form invariant on every intermediate tick.

---

## 11. End-to-end (Image + crop)

```text
Open / nav
  SoftPreview → ScheduleClimb (EscalateToFull)
  PreferCache / Full host → Attach or Async → have covers need → None

Enter crop
  freeze → None

Apply (rect and/or cropRotation)
  want updates → unfreeze
  Soft stand-in possible → FullSource from soft host
  need unmet → est / climb / Async until native bake
  have covers need → None

Zoom
  need recalculated → same decide rules
```

Crop rotation is **not** a separate resolution machine: it is a **want** field.
Changing it forces rematerialize, then the same host climb and post-crop
upgrade rules as any other content edit.
