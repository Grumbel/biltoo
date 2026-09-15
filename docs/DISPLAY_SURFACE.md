<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Display surface controller (host pixel install)

**Status:** design lock for implementation tips. Normative for moving quality /
install / rematerialize **out of ImageView**.

**Related:** [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md),
[PATH_RASTER_SERVICE.md](PATH_RASTER_SERVICE.md),
[PIXEL_HOST_CACHE.md](PIXEL_HOST_CACHE.md),
[THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md),
[IDENTITY.md](../IDENTITY.md), [SIZE.md](../SIZE.md),
[GALLERY_SOFT.md](GALLERY_SOFT.md),
[PIXEL_PIPELINE_REDESIGN.md](PIXEL_PIPELINE_REDESIGN.md) (thumtoo lower layers).

---

## 1. Problem

Pixel **climb** (path → Soft / PreferCache / Full) and pixel **install**
(host raw → display-ready sample on a surface) are tangled inside `ImageView`,
with parallel copies in `ThumbnailBar` and Gallery soft state.

Symptoms that proved the design is wrong:

1. A permanent 1s timer (`displayQualityWatchdogTick`) re-ran
   `InstallHostBetter` on the Image canvas.
2. `DisplayQuality::checkSurface` compared **pre-crop host** long edge to
   **post-crop shown** long edge → contract never cleared for cropped tiles →
   soft demote + async full every second (pulse).
3. Killing the timer without an event-driven upgrade path left tiles **stuck
   on soft** (climb/install lived in the poller).
4. Crop-draft freezes, settled-gates, and mode flags accumulated as patches on
   the same god-object API (`installDisplayPixels`, `canAcceptDisplaySample`,
   rematerialize, gallery soft mirror).

**Principle:** ImageView (and filmstrip cells) must not *decide* quality.
They report **need** and **apply** display-ready pixels. Policy lives in one
controller used by Image, Gallery, Filmstrip, Workspace, and Slideshow.

---

## 2. Layering (unchanged lower layers)

```text
Thumtoo          durable soft / PreferCache / full (path / URI)
ImageCache       process RAM: path → best unoriented host sample (upward-only)
PathRasterService  path → want/have climb SM; sole scheduler of Soft/Prefer/Full
SessionAppearance  SessionImageId → crop / flip / quarter turns / grade (want)
DisplaySurfaceController  NEW — surface registry, decide(), materialize orchestration
Consumers        ImageView / Gallery tiles / ThumbnailBar / Workspace / Slideshow
                 attach pixels only; no InstallHostBetter loops
```

PathRasterService and ImageCache stay path-keyed. Appearance stays
SessionImageId-keyed ([IDENTITY.md](../IDENTITY.md)). The controller is the
only place that joins **path climb**, **appearance want**, and **surface need**.

---

## 3. Surface model

### 3.1 SurfaceKind

| Kind | Consumer | Typical need edge | Climb policy |
|------|----------|-------------------|--------------|
| `ImageFocus` | Image mode primary item | viewport long edge (capped native) | EscalateToFull |
| `GalleryTile` | Gallery `ImageItem` | cell / pack target | SoftDisplay |
| `FilmstripCell` | ThumbnailBar row | strip thumb size | SoftDisplay |
| `WorkspaceItem` | Workspace free item | on-screen footprint / focus | SoftDisplay; Escalate when focused |
| `SlideshowPhase` | from/to phase buffers | slideshow target edge | EscalateToFull |

### 3.2 Binding

Each surface is a stable `SurfaceId` (controller-allocated `qint64`) with:

```text
path              // decode + PathRaster key (never appearance key alone)
sessionId         // SessionImageId; required when session slot exists
kind              // SurfaceKind
generation        // bump on unbind / session switch / cancel
```

Filmstrip rows resolve session index → `SessionImageId` before bind. Path
duplicates in one session are **different** surfaces (different ids), sharing
one PathRaster entry for the path.

### 3.3 State (one struct per surface)

```text
needEdge           // consumer setNeed()
haveDisplayEdge    // long edge of attached *display* sample (post-materialize)
attachedKind       // None | SoftPreview | FullSource
appliedXform       // ContentXform of attached pixels
wantXform          // from SessionAppearance for sessionId (or identity)
frozen             // crop draft hold — no replace until unfreeze
climbPending       // mirror PathRaster::isClimbPending(path)
```

**SIZE.md:** sample pixels never define layout geometry. Intrinsic remains
`ContentXform::layoutSize(fileNative, want)`.

---

## 4. Decision function (pure, testable)

```text
decide(state, hostRaw?) → Action

Action:
  None
  AttachSoft(QImage display)
  AttachFull(QImage display)
  ScheduleClimb(path, needEdge, ClimbPolicy)
  ScheduleAsyncMaterialize(path, sessionId, want)  // worker full bake
```

### 4.1 Normative rules

1. **`frozen`** → always `None` (draft install uses a separate explicit API).
2. **`appliedXform == wantXform` && `attachedKind == FullSource`** → `None`.
   Host long edge is irrelevant (pre-crop vs post-crop must not compare).
3. **`appliedXform == wantXform` && Soft attached** → may
   `ScheduleAsyncMaterialize` or `AttachFull` if host is already GUI-sized;
   **never** re-`AttachSoft` from the same host as a “quality upgrade.”
4. **`wantXform` changed** → rematerialize once from best host (soft stand-in
   if multi-MP, then one async full).
5. **Never demote** FullSource matching want to SoftPreview.
6. **Climb only** via `PathRasterService::ensure`. No second climb SM.
7. **Materialize** only through `SessionAppearance::materializeDisplay`.
   ImageCache stores unoriented host only.

### 4.2 What replaces DisplayQuality install verdicts

`DisplayQuality::checkSurface` host-vs-shown **must not** drive install.
Optional: keep tier helpers for logging. Install decisions use §4.1 only.

Edge compare, when needed for Soft→Full without xform change, must use
**post-materialize** projected size (or attached display edge vs need), not
raw `ImageCache::longEdge` vs cropped `displayPixelLongEdge()`.

---

## 5. Controller API (sketch)

```text
class DisplaySurfaceController : public QObject {
  SurfaceId bind(SurfaceKind, path, SessionImageId);
  void unbind(SurfaceId);
  void setNeed(SurfaceId, int needEdge);
  void freeze(SurfaceId);      // crop draft
  void unfreeze(SurfaceId);
  void wantChanged(SessionImageId);  // appearance store updated
  void noteHostImproved(path, edge); // from PathRaster rasterImproved
  void installDraft(SurfaceId, hostRaw); // freeze path only

signals:
  void displayReady(SurfaceId, QImage, PixelKind, ContentXform::Value);
  void climbRequested(path, needEdge, ClimbPolicy); // or call PathRaster directly
};
```

Consumers connect `displayReady` → attach on their widget/item. Controller
may own the PathRaster pointer and call `ensure` internally so ImageView does
not branch on ClimbPolicy.

### 5.1 Materialize orchestration

| Host edge vs GUI max | Content want | Action |
|----------------------|--------------|--------|
| ≤ max | any | materialize on GUI → Attach* |
| > max | identity | may attach scaled host as Soft or schedule Prefer/Full climb |
| > max | crop/flip/grade | Soft stand-in (clamp + materialize Soft) **once** + ScheduleAsyncMaterialize |

Async completion → `AttachFull` only if generation matches, not frozen, and
want still equals the scheduled want.

---

## 6. Consumer duties (thin)

### ImageFocus

- Bind on session cursor / load; `setNeed(viewport)`.
- Attach on `displayReady`; layout size from want; paint.
- Crop enter: `freeze` + `installDraft`; Apply/Cancel: store + `unfreeze` +
  `wantChanged`.
- **No** quality timer. **No** `installDisplayPixels` policy body.

### GalleryTile

- Bind per tile; decode window sets needs; SoftDisplay climb.
- Pack/watchdog must **not** InstallHostBetter from ImageCache on a timer.
  Blank recovery: re-`setNeed` / ensure with cooldown, or wait for
  `noteHostImproved`.

### FilmstripCell

- Same controller; remove 1.5s install watchdog.
- Thumb geometry → `setNeed`.

### WorkspaceItem

- Bind per item + session id; grade/crop → `wantChanged` for that id only.

### SlideshowPhase

- Bind from/to paths; event-driven from `noteHostImproved` while active.

---

## 7. Session id and manipulation

| Event | Path climb | Appearance | Surface |
|-------|------------|------------|---------|
| Image ←/→ | ensure new path | bind new sid | rebind; old unbind |
| Duplicate path in session | shared path entry | **new** sid | **new** surface |
| Crop Apply | unchanged | update sid | unfreeze + wantChanged |
| Crop draft | cancel path climb | not in store yet | freeze |
| Flip / rotate / grade | unchanged | update sid | wantChanged → one rematerialize |
| Session close | invalidateAll | clear store | unbind all |

Shared path + two sids → one climb, two materializations (IDENTITY).

---

## 8. Migration (ImageView method map)

| Current (ImageView / bar) | Destination |
|---------------------------|-------------|
| `installDisplayPixels` policy | Controller decide + materialize |
| `canAcceptDisplaySample` | decide() rules §4.1 |
| `attachDisplaySample` | Consumer attach helper (thin) |
| `scheduleAsyncHostRematerialize` / finish | Controller async materialize |
| `rematerializeItemContent` / tryRematerialize | Controller wantChanged path |
| `displayQualityWatchdogTick` Image branch | **Delete** |
| `ThumbnailBar::qualityWatchdogTick` installs | **Delete** |
| Gallery soft InstallHostBetter in pack tick | **Delete**; use noteHostImproved |
| `requestEscalateClimb` / direct ensure from View | Controller setNeed / policy |
| `m_cropDraftSampleFrozen` | surface `frozen` |
| `GallerySoftState` policy mirror | surface state + PathRaster have |

Keep on ImageView: scene, modes, input, pack geometry, crop chrome, paint.

---

## 9. Implementation phases

| Phase | Deliverable | Status |
|-------|-------------|--------|
| **A** | This doc + TODO tip | done (953) |
| **B** | `decide()` + controller skeleton + tests | done (954) |
| **C** | ImageFocus drive + PathRaster events | done (955) |
| **D** | Image off quality timer; event-only | done (956) |
| **E–F** | Gallery / filmstrip via decide | done (957, 959) |
| **G** | Workspace delivery + slideshow phases | done (958, 960) |
| **H** | No install path calls checkSurface | done (961) |
| **H2** | Delete unused checkSurface / reportViolation | done (962) |

**Residual:** per-tile `SurfaceId` bind/unbind lifecycle (optional); ImageView
still owns materialize/attach helpers called *after* decide.

Each phase must leave the app usable: soft still appears; full still arrives
via **events**, not a 1s InstallHostBetter poller.

---

## 10. Anti-patterns (do not reintroduce)

- Permanent timer calling InstallHostBetter on Image or Filmstrip.
- Comparing pre-crop host edge to post-crop shown edge to decide install.
- Soft-demoting FullSource that already matches store want.
- Second climb state machine outside PathRasterService.
- Storing crop-baked display in ImageCache.
- Path-only appearance when SessionImageId exists.
- “Fix pulse” by more freezes without event-driven full delivery.

---

## 11. Test checklist (Phase B+)

- frozen → None  
- FullSource + equal xform → None even if hostEdge ≫ haveDisplayEdge  
- Soft + equal xform + large host → ScheduleAsyncMaterialize only  
- want xform change → rematerialize  
- two sids, one path → independent surfaces  
- generation mismatch drops stale async full  

---

## 12. Summary

One controller joins path climb, session appearance, and surface need.
Consumers only bind, setNeed, freeze/unfreeze, and attach. No ImageView
quality watchdog. No host-vs-cropped-shown install loop. Soft→full is a
one-shot event chain, not a 1s cycle.
