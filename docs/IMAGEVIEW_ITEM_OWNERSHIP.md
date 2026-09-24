<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# ImageView / ImageItem ownership (Phase 5 / 0.3 track)

Companion to [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md) (mode stashes) and
[CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) (who owns *want*).

This document is the **target graph** for shrinking `ImageView` as a god-object
and keeping `ImageItem` a thin scene node. Dual ImageView (0.3) depends on this
not being two copies of a 12k-line façade.

## Roles

| Object | Role |
|--------|------|
| **`ImageItem`** | Scene node: path, session id, display pixels, live pose, paint/hit chrome. **Does not** own tile bags, climb policy, or durable appearance. |
| **`ImageView`** | `QGraphicsView` shell + public API. Host for mode switches, selection, and thin **sole-external** mutators into private `ImageItem` fields (friend). Must not grow new pixel/tile authority. |
| **`DisplayPipelineController`** | Owns PreferCache climbs, **tile LOD bags** (Stage 2), `installDisplayPixels`, soft/full install. Friend of `ImageItem` for pixel/tile mutators. |
| **`ImageController`** | Image-mode enter, classic path, session edge keys, reload. Does not install pixels directly — uses pipeline / view host. |
| **`GalleryController` / `WorkspaceController`** | Mode enter/leave, stashes, pack/free-form. Cell size / intrinsic via ImageView hosts (`setItemIntrinsicSize`), not ImageItem friends. |
| **`SlideshowController`** | Overlay path; reuses pipeline tile sessions (cover paint), must not create a parallel pixel store. |
| **`ItemWorld`** | Durable sparse appearance (crop/orient/grade) keyed by `SessionImageId`. |
| **`CropSession`** | Draft crop helpers only — no ImageItem friend; geometry via ImageView hosts. |

## Who may create / destroy `ImageItem*`

| Action | Owner |
|--------|--------|
| Create live item | `DisplayPipelineController` / load paths via `ImageView` host (`createItem*`) |
| Destroy live item | `ImageView` canvas clear / focus paths (detach tile bag **before** delete) |
| Off-scene stash | Mode controller only (`WorkspaceController::m_stashedItems`, `GalleryController::m_stashedItems`) — see MODE_OWNERSHIP |
| Image underlay | **New** underlay from load pipeline — **never** steal a Workspace stash pointer |

## Who may install pixels

| Path | API |
|------|-----|
| Primary install | `DisplayPipelineController::installDisplayPixels` (sole ImageItem friend for pixels) |
| Attach already-materialized sample | `DisplayPipelineController::attachDisplaySample` (sole place that calls `setPreviewImage` / `setSourceImageReady`); `ImageView::attachDisplaySample` forwards |
| Content layout intrinsic | `DisplayPipelineController::applyContentLayoutSize` (file-native × want); called from attach and from bake/crop hosts |
| Rematerialize (host → display) | `DisplayPipelineController::rematerializeItemContent` / `scheduleAsyncHostRematerialize` |
| Soft preview only | `DisplayPipelineController::hostSetPreviewImage` / `installDisplayPixels`; ImageView forwards |
| Clear display pixels | `ImageView::clearItemDecodedPixels` **or** pipeline (friend) during install/replace |
| Intrinsic / layout size | `DisplayPipelineController::hostSetIntrinsicSize` (Gallery LQIP guard); ImageView forwards |

**Forbidden:** ad-hoc `item->setSourceImageReady` / `setPreviewImage` / `clearDecodedPixels` outside the hosts above.
**Removed:** `ImageItem::setSourceImage` (legacy geometry re-entry path); sole install mutators are Ready + Preview.

## Who owns tile LOD

| Concern | Owner |
|---------|--------|
| `tilelod::ItemBag` allocation / map | `DisplayPipelineController` |
| Attach / detach bag on item | Pipeline only (`attachTileLodBag` / `detachTileLodBag`) |
| Session viewport / tick / paint | Bag’s `TileLodController`; ImageItem reads via attached bag |
| Shared path RAM cache | `TileLodRegistry` (process-wide) |
| Suppress flag | Pipeline writes `bag.suppressed`; item is not a parallel authority |

ImageItem `tileLodBag()` asserts a pipeline-owned bag (Stage 2). Orphan static bag is debug-only.

## Who owns pose

| Concern | Owner |
|---------|--------|
| Live pose fields | Only `applyPlacement` / `GalleryLayout::applyItemPlacement` (Stage 2 single writer) |
| Workspace chrome drag | Item interaction → `applyPlacement` |
| Durable pose | Workspace `WorkspaceItemState` / ItemWorld as documented |

## ImageView is not a friend of ImageItem

**Done (2417+):** `friend class ImageView` removed.
**Done (2421+):** CropSession / GalleryController / GalleryLayout friends removed.
**Done (2422):** dead `ImageItem::setSourceImage` removed — Ready + Preview only.

1. Canvas host surface is **public** on `ImageItem` (pose, session bind, mode chrome, applied xform, colour).
2. Pixel / path / tile mutators stay **private**; **sole friend** is `DisplayPipelineController`.
3. ImageView reaches pixels only via `DisplayPipelineController::hostClearDecodedPixels` /
   `hostSetIntrinsicSize` / `hostSetPreviewImage` / `attachDisplaySample` / `installDisplayPixels`.
4. Intrinsic layout after content orient/crop is `DisplayPipelineController::applyContentLayoutSize`
   (ImageView forwards; attachDisplaySample invokes it).

## Mode vs item (pointer ownership)

See [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md). Summary:

- `liveItems()` = on-scene only for the **active** mode.
- Stashes exclusive for off-scene items.
- Image mode underlay ≠ Workspace tile pointer.

## Extraction backlog (ordered)

1. **Done:** graph written; ImageView clear/intrinsic via host wrappers.
2. **Done:** `attachDisplaySample` implementation on `DisplayPipelineController`; view one-line forward.
3. **Done:** `friend class ImageView` removed; public host surface + pipeline pixel hosts.
3b. **Done:** Drop CropSession / GalleryController / GalleryLayout friends; crop intrinsic via `setItemIntrinsicSize`.
3c. **Done:** Remove dead `ImageItem::setSourceImage`; sole private install paths are `setSourceImageReady` + `setPreviewImage`.
3d. **Done:** `applyContentLayoutSize` owned by `DisplayPipelineController`; ImageView one-line forward;
    redundant post-`attachDisplaySample` layout calls removed.
3e. **Done:** `rematerializeItemContent` + async host rematerialize owned by pipeline;
    ImageView forwards; tryRematerializeFromHost on pipeline (public for bake/crop restore).
3f. **Done:** Bake/crop restore call pipeline try/rematerialize; dead view decls removed.
3g. **Done:** bake pixel path uses `rematerializeItemContent` (want/undo stay on view).
3h. **Done:** cold-cache disk soft stand-in lives in pipeline `rematerializeItemContent`;
    bake no longer loads thumbnails itself.
3i. **Done:** `rematerializeGalleryItemFromStore` owned by pipeline; ImageView forward.
3j. **Done:** interactive color-grade SoftPreview via `installInteractiveSoftPreview` on pipeline.
3k. **Done:** `reinstallModePixelsAfterIdentityReset` (Gallery soft / Image full) on pipeline.
3l. **Done:** `clearStaleAppliedFingerprintIfNeeded` on pipeline (pairs with gallery rematerialize).
3m. **Done:** `bakeItemRotate90` / `bakeItemFlip` orchestration on pipeline;
    ImageView host helpers for capture/crop-map/persist/undo; thin forwards.
3n. **Done:** Gallery LQIP intrinsic guard on `hostSetIntrinsicSize`; layout uses host path.
3o. **Done:** pipeline no longer detours via ImageView for preview/rematerialize install.
3p. **Done:** mode controllers call hostDisplayPipeline for clear/attach/rematerialize/intrinsic.
3q. **Done:** ImageView TUs call m_displayPipeline directly (no self-forward hop).
3r. **Done:** Phase 6 Tier 0 — privatize pipeline-forward host methods (2437).
3s. **Done:** `setItemIntrinsicSize` private; all writers use `hostSetIntrinsicSize` (2438).
3t. **Done:** Tier 0 privatize `sessionIdMatchesPath`. `restoreStickyPanAnchor` remains public host (paired with capture; characterization).
3u. **Done:** `bakeItemFlip` private; chrome/toolbar call pipeline bakeItemFlip.
3v. **Done:** drop ImageView::bakeItemRotate90 forward; rotateContent uses pipeline only.
3w. **Done:** drop ImageView bake TU; pipeline sole bake entry.
3x. **Done:** drop imageview_rematerialize.cpp and dead private pixel/layout forwards;
    callers use DisplayPipelineController exclusively.
3y. **Done (status):** Phase 5 pixel/layout/bake ownership complete; Phase 6 Tier 0
    exit criteria met (~693-line imageview.h, ~217 public methods, sole friend =
    DisplayPipelineController).
4. Dual ImageView shares pipeline + ItemWorld, not a forked façade (0.3 product track).
5. Residual (not pixel ownership): interactive grade live-grade + filmstrip emit policy;
    bake host helpers + sticky capture + clearTextSelection public for pipeline;
    `rotateContentByQuarterTurns` public (chrome + framing after pipeline bake).

6. Phase 6 (REFACTOR.md): header closure, then paint/input/size-book collaborators.
7. **biltoo-2464:** Workspace multi-select group scale/rotate owned by
   `WorkspaceController` (`workspace_group.cpp`); ImageView is input router only.
8. **biltoo-2465:** Page-guide session + behaviour on WorkspaceController.
9. **biltoo-2466:** Workspace chrome try* + ItemInteractSession on WorkspaceController.
8. **biltoo-2465:** Print page-guide (`PageGuideSession` + resize/chrome) owned by
   `WorkspaceController` (`workspace_pageguide.cpp`); ImageView thin API + print render.


## Phase 5 completion (biltoo-2443+)

**Pixel / layout / content-bake install is owned by `DisplayPipelineController`.**

| Concern | Owner |
|---------|--------|
| Private ImageItem pixel/path/tile mutators | Sole friend: DisplayPipelineController |
| attach / rematerialize / async / cold disk / SoftPreview grade | Pipeline |
| Content bake rotate/flip | Pipeline (`bakeItemRotate90` / `bakeItemFlip`) |
| Intrinsic size (incl. Gallery LQIP guard) | `hostSetIntrinsicSize` |
| Mode controllers | `hostDisplayPipeline()` — no ImageView pixel hop |
| ImageView pixel thin forwards | Removed (bake + rematerialize TUs deleted) |

**Phase 6 Tier 0 (header closure):** `imageview.h` ~693 lines; public method count
~217 (exit target &lt;1000 lines, &lt;260 public). Further privatizations are optional
polish; remaining public surface is largely host API for controllers/pipeline.


## Dual ImageView (0.3) prerequisites

Prerequisite for [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §4.8: two focused Image-mode
surfaces **share** pipeline + ItemWorld rather than forking the façade.

### Already satisfied (Phase 5)

| Prerequisite | Status |
|--------------|--------|
| Sole ImageItem pixel friend = DisplayPipelineController | Done |
| No ImageView pixel thin-forward layer | Done (bake + rematerialize TUs gone) |
| Controllers install via `hostDisplayPipeline()` | Done |
| Content bake on pipeline | Done |
| Intrinsic sole writer `hostSetIntrinsicSize` | Done |
| ItemWorld as sparse durable appearance | Done (Stage 4b+) |
| `imageview.h` / public surface within Tier 0 exit | Done (~693 lines / ~217 public) |

### Still required for dual surface

1. **Two live Image-mode canvases** without double-owning `m_items` / mode stashes —
   likely one shell with two scene roots, or two thin views over shared controllers.
2. **Shared** `DisplayPipelineController` + `ItemWorld` + session id book — not two
   pipelines writing the same SessionImageId.
3. **Focus / selection model** that can target left vs right without path-only
   ambiguity (prefer SessionImageId).
4. **Framing** per surface (`ViewFraming` / sticky pan) independent while content
   bake remains session-global.
5. **Do not** reintroduce ImageView pixel mutator friendship or parallel install paths.

### Topology (target)

```
SessionShell (MainWindow region or thin owner)
  ├── ItemWorld              (shared, durable appearance)
  ├── SessionDocument        (shared, id book)
  ├── DisplayPipelineController (shared; installs by SessionImageId + active host)
  ├── ImageView left         (scene, liveItems, framing, input, paint)
  └── ImageView right        (scene, liveItems, framing, input, paint)
```

Each surface implements `DisplayPipelineHost`. Pipeline dual-critical paths use
`host()` (Stage 0); long-tail still uses `view()` until Stage 1 migrates the rest
onto the host surface. Content bake remains session-global (ItemWorld); framing
and sticky pan stay per-surface.

### Stage 0 (landed — biltoo-2449)

- `DisplayPipelineHost` — virtual surface for dual-critical state:
  ItemWorld, liveItems, canvasScene, mode queries, SessionDocument,
  hostSessionId / hostSizeBook / hostPathRaster / hostBindBook / hostSeedBook,
  logicalSizeForPath, viewportWidget.
- `ImageView` implements `DisplayPipelineHost` (public).
- `DisplayPipelineController` holds `m_host` + `m_view`; Stage 0 call sites use
  `m_host->…` for the dual-critical surface above.

### Stage 1 (landed — biltoo-2450)

- Host grows: controller bags (slideshow/crop/gallery/image/framing/
  gallerySizeResolve), appearance helpers (sessionAppearanceValue,
  applied ContentXform, sync live meta/color, contentLayoutSize,
  sessionListIndex, setItemSessionId), primary/target items, Image-mode
  framing/sticky anchors, setUpdatesEnabled, notifyStatusChanged /
  notifyWorkspacePathsChanged, hostObject().
- Pipeline migrates those sites to `m_host` (~471 host calls; ~76 residual
  on `m_view` for tr(), load/place helpers, text layer, pack order, etc.).
- `QPointer<ImageView>` guards and timer parents still use `m_view` (async
  lambdas call ImageView-only APIs such as matchesLoadGeneration /
  hostDisplayPipeline).

### Stage 1b (landed — biltoo-2451)

- Residual long-tail migrated onto host: tr→hostTr, text/workspace/layout
  bags, load/place/pack helpers, content-bake host ops, mapFromScene /
  mapViewportToScene / devicePixelRatioF, etc.
- **Zero** non-comment `m_view->` call sites in pipeline TUs.
- `m_view` retained only for: QPointer guards, QTimer parents, TileLoadCoordinator
  ctor, and null-guard transitions (now prefer `m_host`).
- Self-call `m_view->hostDisplayPipeline().loadGate()` → `loadGate()`.

### Stage 2a (landed — biltoo-2452)

- Async jobs (`displaypipeline_jobs`) take `QPointer<QObject> life` +
  `DisplayPipelineController *` — no `QPointer<ImageView>`.
- `TileLoadCoordinator` bound to pipeline (host via `pipe->host()`); uses
  `viewTransform()` / `mapViewportToScene` on host.
- QTimer parents and `QTimer::singleShot` contexts use `hostObject()`.
- Inline worker lambdas capture `life` + `pipe`; call pipeline methods
  directly (no `hostDisplayPipeline()` hop).
- `m_view` retained only for ctor/`view()` API.

### Stage 2b (landed — biltoo-2453)

- Removed `ImageView *m_view` and `view()` from DisplayPipelineController.
- Constructor is `DisplayPipelineController(DisplayPipelineHost *host)` only.
- ImageView still constructs the pipeline with `this` (implements host).

### Stage 2c.0 (landed — biltoo-2455)

Infrastructure only (no dual-pane product UI yet):

- `DisplayPipelineController::setActiveHost(DisplayPipelineHost *)` — GUI-thread
  switch of the host that owns live items / viewport for PreferCache installs
  and tile coordinator queries. Does **not** reparent QTimers (they stay on the
  `hostObject()` used at creation time).
- `ImageView::bindSharedItemWorld(ItemWorld *)` — non-owning share of durable
  appearance across hosts. Null reverts to the view-owned `ItemWorld`. External
  world must already have path/size books bound by its owner.
- Single-pane path unchanged: ctor still constructs pipeline with `this`;
  `itemWorld()` returns the owned world when no share is bound.

### Stage 2c.1 (landed — biltoo-2456)

Shared pipeline ownership plumbing (still no dual-pane product UI):

- `DisplayPipelineController` is held as `unique_ptr` (`m_ownedPipeline`) with a
  non-owning `m_displayPipeline *` used by all ImageView TUs.
- `ImageView::bindSharedDisplayPipeline(pipeline)` releases the owned controller
  and points at an external one (shell or primary host remains lifetime owner).
- `hasSharedDisplayPipeline()` detects external bind.
- Single-pane: ctor `make_unique<DisplayPipelineController>(this)` — behaviour
  unchanged aside from pointer indirection.

**Wire order for a future dual shell**

1. Create primary `ImageView` (owns pipeline + ItemWorld by default).
2. Create secondary `ImageView`.
3. `secondary->bindSharedItemWorld(&primary->itemWorld())` — or both bind to a
   shell-owned ItemWorld (preferred when path/size books are also shared).
4. `secondary->bindSharedDisplayPipeline(&primary->hostDisplayPipeline())`.
5. On pane focus: `pipeline.setActiveHost(focusedHost)`.

### Stage 2c.2 (landed — biltoo-2457)

Product shell (minimal):

- `DualImageShell` (`src/shell/dualimageshell.{h,cpp}`) — QSplitter, primary +
  optional secondary `ImageView`, focus → `setActiveHost` on the shared pipeline.
- MainWindow central widget is the shell; `m_imageView` remains the primary host.
- View → **Dual compare** (`Ctrl+Shift+D`) enables Image-mode side-by-side.
  Secondary binds `bindSharedItemWorld` + `bindSharedDisplayPipeline` from primary.
- Session navigation / filmstrip / Gallery still drive the **primary** only.
- Secondary starts empty (compare load / lock-step nav is later product work).

### Stage 2c.3 (landed — biltoo-2458) / 2461 dual display fix

- **2461:** Dual secondary uses **per-surface** DisplayPipeline (shared ItemWorld
  only). Shared pipeline + one active host dropped secondary installs → black pane.

### Stage 2c.3 (landed — biltoo-2458)

- `DualImageShell::openOnSecondary(path, sid)` — setActiveHost(secondary), classic
  path + session id, `ImageController::enter` / `loadImage`.
- `navigateSecondary(delta, paths, ids)` — independent compare nav (wraps).
- Dual enable seeds secondary with the **next** session row (or same if n==1).
- `goPrevious` / `goNext`: when secondary is focused, step secondary only;
  primary session cursor (`m_currentIndex`) unchanged.
- Secondary `navigatePrevious/NextRequested` connected to the same slots.

### Stage 2c (remaining)

- Optional lock-step ←/→ (both panes advance together).
- PreferCache / focus surface rules when two panes show different SessionImageIds:
  - PreferCache climb targets the **active** host's primary/target item.
  - Inactive host may still paint already-installed samples; do not run a second
    parallel PreferCache for the same SessionImageId.
  - Tile coordinator viewport is the active host's `mapViewportToScene()`.
  - Framing / sticky pan remain **per host** (ViewFraming on each surface).
- QTimer / async lifetime: either reparent timers on host switch, or own them on
  a neutral shell QObject so they outlive focus changes.

### Residual on single ImageView (ok to keep)

- `applyInteractiveColorGrade` live-grade + filmstrip emit (view policy / signals)
- Bake host helpers (`captureContentBakeBeforeState`, undo push, persist) for pipeline
- `rotateContentByQuarterTurns` (pipeline bake + this-surface framing)

## Related

- [MODE_OWNERSHIP.md](MODE_OWNERSHIP.md)
- [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md)
- [TILE_LOD.md](TILE_LOD.md) / [TILE_LOAD_COORDINATOR.md](TILE_LOAD_COORDINATOR.md)
- [RELEASE_0.2.0.md](RELEASE_0.2.0.md) §4.8 (0.3 dual ImageView depends on this)
- `src/display/displaypipelinehost.h`

10. **biltoo-2467:** Image-mode framing/sticky pan behaviour on ImageController; ViewFraming bag stays on host.

11. **biltoo-2468:** ImageSizeBook + probe/remember on ImageSizeCoordinator; GallerySizeResolve host remains on ImageView.

12. **biltoo-2469:** GallerySizeResolveHost + gate owned by GalleryController.

13. **biltoo-2471:** Complete Workspace input ownership — page-guide move/release,
    group/handle/item-drag move/release on `WorkspaceController`; ImageView thin
    routers only (closes residual after 2464–2466).

14. **biltoo-2472:** GalleryDecodeBook owned by GalleryController; hostGalleryDecodeBook
    forwards; ImageView / pipeline / shell use host surface only.

15. **biltoo-2473:** LayoutPrefs + LayoutDebounce + GalleryRelayoutSuppress +
    LayoutApplyGuard owned by GalleryController; hostLayout* forwards; debounce
    QTimer stays on ImageView.

16. **biltoo-2474:** Image-mode edge hover + affordance paint on ImageController
    (EdgeNavPolicy::Zone); ImageView::EdgeZone remains public host enum with
    conversion bridges.

17. **biltoo-2475:** ZoomRegionGesture (Z-key / Workspace Zoom tool) owned by
    ImageController; ImageView thin arm/cancel/try* routers.

18. **biltoo-2476:** TextLayerController owns TextLayerSession + text/link
    search, rubber-band, hit-test, and paint helpers; ImageView thin routers.

19. **biltoo-2477:** Tool (Select/Pan/Zoom) owned by WorkspaceController;
    ImageView setTool/currentTool thin forwards.

20. **biltoo-2478:** SessionNavFlags owned by ImageController (with edge nav);
    hostSessionNav forwards.

21. **biltoo-2479:** ColorAdjustCommit bag owned by ImageController; commit
    QTimer remains on ImageView (same pattern as gallery layout debounce).

22. **biltoo-2480:** Gallery layout-debounce QTimer owned by GalleryController
    (parented to ImageView shell); completes 2473 residual.

23. **biltoo-2481:** Colour-adjust commit QTimer owned by ImageController
    (parented to ImageView); completes 2479 residual.

24. **biltoo-2482:** Gallery decode-watchdog QTimer owned by GalleryController;
    slideshow phase surface tick moved onto the slideshow progress timer.

25. **biltoo-2483:** HudChrome owns HudAppearance + HudFlash + flash QTimer;
    ImageView thin flashHud / hostHud* forwards.

26. **biltoo-2484:** PendingItemAppearanceBook owned by SessionBindBook
    (clears with bind queue on session wipe).

27. **biltoo-2485:** Status-refresh coalesce QTimer owned by HudChrome
    (parented to ImageView); Gallery keeps its own scheduleStatusRefresh timer.

28. **biltoo-2485.2 / 2486:** HudChrome drops QWidget include (callback update);
    SlideshowController owns progress QTimer (parented to ImageView).

29. **biltoo-2487:** PathRasterService owned by DisplayPipelineController;
    hostPathRaster forwards to pipeline pathRaster().

30. **biltoo-2488:** CentreProgress owned by HudChrome; drop unused ImageView
    gallery decode constants (limits live in GalleryDecode).

31. **biltoo-2489:** ViewShellChrome owns ViewportChrome + CanvasBackground;
    group-transform residual comment cleaned (already on WorkspaceController).

32. **biltoo-2490:** SessionShell owns SessionIdentity, SessionBindBook, and
    PackOrderOverlay; hostSessionId / hostBindBook / pathOrder* forward to m_session.

33. **biltoo-2491:** PerfStats owned by HudChrome (HUD overlay diagnostics);
    TileNeighborPrefetch stays on ImageView (per-view pathOnLiveCanvas for dual pane).

34. **biltoo-2492:** Residual inventory — intentional ImageView shell/host bags
    after the mode-ownership transfer series (2471–2491):

    | Residual | Why it stays on ImageView |
    |----------|---------------------------|
    | ViewFraming | **Moved to ImageController (2493)**; hostFraming forwards |
    | ViewMode | Mode shell enum |
    | ViewShellChrome | Viewport + canvas materials (QGraphicsView shell) |
    | HudChrome | Cross-mode HUD overlay |
    | SessionShell | Session identity / bind / pack-order host surface |
    | TileNeighborPrefetch | Per-view pathOnLiveCanvas (must not share across dual pane) |
    | ImageSizeCoordinator | SIZE.md host + GallerySizeResolve surface |
    | ImageModeSoftProvider | Injection from MainWindow / dual shell |
    | ItemWorld / PathItemStateBook | Data domain bound on host |
    | QUndoStack | Cross-mode undo shell |
    | DisplayPipelineController | Shared or owned pipeline pointer |

    Mode bags (Workspace / Gallery / Image / Text / Slideshow / Crop) and
    PathRasterService live on their controllers.

35. **biltoo-2493:** ViewFraming owned by ImageController (per-view; dual-safe);
    hostFraming forwards to m_image.framing().

36. **biltoo-2494:** WorkspaceController::applyToolDragMode owns Select
    rubber-band drag-mode sync (pairs with tool ownership 2477).

37. **biltoo-2495:** WorkspaceController owns stack raise/lower (scene-overlap
    z-order) and opacity up/down/reset; ImageView thin routers. Helpers live in
    workspace_stack.cpp beside group/page-guide.

38. **biltoo-2496:** WorkspaceController owns placement resets (scale / rotation /
    shear) for Workspace selection and Image target/primary; ImageView thin
    routers. Completes transform_actions Workspace geometry ownership with 2495.

