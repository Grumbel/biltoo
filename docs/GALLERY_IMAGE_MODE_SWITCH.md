<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery ↔ Image mode switch latency

Status: **2026-10-01** — static analysis + measurement plan. No runtime
numbers in-tree yet; enable env hooks below and paste exceeds into TODO when
collected.

Related: [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md) (one scene, stashes),
[GALLERY_PIXELS.md](GALLERY_PIXELS.md), [IMAGE_MODE_NAV_SOFT.md](IMAGE_MODE_NAV_SOFT.md),
[IMAGEVIEW_SURFACE.md](IMAGEVIEW_SURFACE.md), [PERFORMANCE.md](PERFORMANCE.md).

## Symptom

Going Gallery → ImageView and back shows a short but noticeable hitch (order of
a fraction of a second). Ideal: keep the last Gallery presentation ready and
return to it instantly.

## What already exists

Modes are **presentations** over shared data, not three independent apps.

| Structure | Role |
|-----------|------|
| `SessionDocument` | Membership (paths + `SessionImageId`) |
| `ItemWorld` | Appearance / crop identity |
| `TileLodRegistry` / ImageCache | Pixels shared across modes |
| `GalleryController::m_stashedItems` | Packed cells kept off-scene while in Image |
| `WorkspaceController::m_stashedItems` | Free-form tiles kept while in Image/Gallery |
| Single `QGraphicsScene` on primary `ImageView` | **Active mode only** on-scene |

**Gallery → Image:** `onLeave` → `stashItems()` removes live items from the
scene and holds pointers (+ pack order). Items are not destroyed.

**Image → Gallery (warm):** `enter` → `restoreStashedItems()` when the Gallery
stash is non-empty. Documented path **skips** full `applyLayout(EnterGallery)`
decode storm; still runs virtual plan / window / decode-window bookkeeping.

**Image underlay is not stashed.** Image enter always goes through
`ImageController::enter` → `loadImage` (host-raw cache / LQIP / tiles). Mode
stashes must not be stolen as the Image underlay ([MODE_OWNERSHIP.md](MODE_OWNERSHIP.md)).

## Call chain (warm Gallery ↔ Image)

### Gallery → Image

1. `ImageView::setViewMode(Image)` — `GUI_BUDGET("ImageView::setViewMode")`
2. Previous Gallery: `GalleryController::onLeave` → `stashItems()` (removeItem × N)
3. Residual live clear if needed (`clearLiveCanvas`)
4. `ImageController::enter` — `GUI_BUDGET("ImageController::enter")`
   - `prepareImageModeCanvas`, clear live
   - `hostDisplayPipeline().loadImage(classicPath)` — new underlay, not a
     promoted Gallery cell
5. First paint of Image underlay

### Image → Gallery (stash non-empty)

1. `setViewMode(Gallery)` — leave Image (underlay destroyed, not stashed)
2. `GalleryController::enter` with `restoredStash`
   - Optional paint hold (`viewport()->setUpdatesEnabled(false)`)
   - `restoreStashedItems()` — re-parent N items
   - Per-item mode flags + `applyPlacement` (upright overview when not a
     layout-only switch)
   - `rebuildVirtualPlan` + `syncVirtualWindow` — budgets on both
   - `setSceneRect` / scrollbars / camera restore
   - `updateDecodeWindow` — `GUI_BUDGET("updateGalleryDecodeWindow")`
   - Re-enable updates + one `update()`

Cold enter (empty stash) is a different path: `populateGalleryCanvas`,
size-resolve, placeholders, full pack — not the focus of the “instant return”
complaint when the user only opened one image and came back.

## IO vs scene / CPU

| Cause | Gallery → Image | Image → Gallery (warm) |
|-------|-----------------|-------------------------|
| Disk / cold thumtoo open | Unlikely if path warm in ImageCache + tile registry | Unlikely for restore itself; decode window may **schedule** tiles |
| Scene graph | Detach N Gallery items; build 1 Image underlay | Re-add N items; sceneRect / scrollbars |
| Presentation CPU | `loadImage` + framing + install | Placement pass + virtual plan + decode-window scan |
| Full Gallery relayout | No (stash) | No (warm path avoids full `applyLayout`) |

Conclusion from code read: lag is consistent with **presentation rebuild on
one shared scene**, not with discarding Gallery and reloading every file from
disk. Stash is real; “instant flip” is not — leave/enter still does real GUI
work.

## Measurements (do this before structural changes)

```bash
BILTOO_GUI_BUDGET_LOG=1 BILTOO_MODE_DEBUG=1 BILTOO_PERF=1 biltoo /path/to/session
```

| Hook | Scope |
|------|--------|
| `ImageView::setViewMode` | Whole switch shell |
| `ImageController::enter` | Image attach + `loadImage` |
| `GalleryController::rebuildVirtualPlan` | Warm restore plan |
| `GalleryController::syncVirtualWindow` | Visible live window |
| `updateGalleryDecodeWindow` | LQIP / tile interest after restore |
| `GalleryController::applyLayout` | Full pack — **must not** appear on warm Image↔Gallery |
| `biltooModeDbg` (`BILTOO_MODE_DEBUG`) | live / wstash / gstash counts |

Default GUI budget is **25 ms** per scope; chained scopes in one event-loop
burst also report (`biltoo_thread.h`). Performance panel + `BILTOO_PERF` cover
paint / decode-window timings.

**Questions for a single round-trip log:**

1. Is wall time in `setViewMode`, `ImageController::enter`, Gallery restore
   scopes, or first paint after re-enable?
2. On return, does `applyLayout` fire? (If yes → bug vs documented warm path.)
3. Is `gstash` non-zero on Gallery leave and consumed on enter?

Until those numbers exist, optimize the wrong layer (widget swap vs slim
decode window) is guesswork.

## Architectural options

### A. Slim the warm path (lowest risk)

Keep one scene + stash. Cut work when restore is known-valid:

- Defer or skip `updateDecodeWindow` when coverage is already warm
- Skip `rebuildVirtualPlan` if pack order and scene bounds unchanged
- Avoid redundant per-item `applyPlacement` when poses are already correct
- Image enter: faster warm underlay (cache hit only; still no stash steal)

Matches current ownership rules. Prefer this once budgets identify the hot
scopes.

### B. One scene, leave Gallery items in-scene but inactive

On Gallery → Image: do not `removeItem`; gate paint / hit-test / `liveItems`
so Gallery cells are inert while the Image underlay is shown. Return = flip
gates + camera.

Pros: no re-parent storm. Cons: large scene always resident; must rework
`liveItems()`, `clearLiveCanvas`, virtual window, and bleed rules (Image
underlay must not appear as Workspace content).

### C. Two scenes, `QGraphicsView::setScene`

Permanent Gallery scene vs Image scene; switch with `setScene` + restore
transform/scroll.

Pros: isolated item trees. Cons: dual host wiring (selection, rubber band,
tile-LOD surface, scrollbars); items cannot belong to two scenes; still need
Image underlay policy.

### D. Two widgets, hide/show or `QStackedWidget`

Separate Gallery-capable `ImageView` and Image `ImageView`; show one.

Pros: can feel instant if both surfaces are already painted. Cons: large
shell redesign — every `m_imageView` consumer, chrome, filmstrip, crop,
slideshow, dual-compare primary (`DualImageShell` already uses a second view
for **Image vs Image** compare, not mode isolation). Memory: full Gallery
item tree + Image pipeline kept alive.

**Not a small lag fix.** Same scale as dual-shell product work.

### E. Viewport pixmap hold

Snapshot Gallery viewport; show static pixmap while real restore runs.
Perceived instant only; interaction waits for real items.

## Why not “just hide/show” by default

[MODE_OWNERSHIP.md](MODE_OWNERSHIP.md) § One scene, not three:

> Three scenes would isolate transforms and items, but would also triplicate
> selection, scrollbars, chrome, and tile-LOD host wiring, and force
> cross-scene reparenting on every switch. Isolation is the **mode switch
> pipeline**, not the number of scenes.

Pipeline today:

1. Snapshot previous presentation into mode data (stash / durable)
2. Detach live — nothing from the previous mode remains on-scene
3. Set active mode
4. Attach destination (stash restore, `loadImage`, or cold Gallery pack)

Instant switch requires either **not doing that work** (keep both
presentations ready and only change visibility) or **hiding the cost**
(pixmap hold / async). Qt does not make a fully populated single scene free
to rebuild on every mode change.

## Recommended sequence

1. Capture one Gallery → Image → Gallery cycle with budget + mode debug logs.
2. Record exceeded scopes and stash counts in TODO (or extend this doc).
3. If restore cost is optional bookkeeping → **A**.
4. If cost is dominated by re-parent / scene indexing → consider **B** before
   **C**/**D**.
5. Treat **D** as an explicit architecture project, not a drive-by.

## Code map

| Area | Location |
|------|----------|
| Mode shell | `src/view/imageview_routers.cpp` — `setViewMode`, `setActiveMode` |
| Gallery leave/enter/stash | `src/gallery/gallerycontroller.cpp` — `onLeave`, `enter`, `stashItems`, `restoreStashedItems` |
| Gallery focus / `enterGallery` | `src/gallery/gallerycontroller_focus.cpp` |
| Image enter | `src/image/imagecontroller.cpp` — `enter` |
| Decode window | `GalleryController::updateDecodeWindow` |
| Virtual plan / window | `rebuildVirtualPlan`, `syncVirtualWindow` |
| GUI budget | `src/util/biltoo_thread.h` — `GUI_BUDGET`, 25 ms default |
| Mode dbg | `src/util/biltoo_logging.h` — `biltooModeDbg` / `BILTOO_MODE_DEBUG` |
| Ownership contract | `docs/MODE_OWNERSHIP.md` |
