<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Biltoo structural refactor

This document is the plan of record. It follows DOMAIN.md / IDENTITY.md / SESSION.md.
Implementation must not invent a second domain model.

## Goals

1. **One source of truth for session image appearance** (crop, content flips, quarter turns)
   keyed by `SessionImageId`, never by path alone when duplicates exist.
2. **Gallery packing is explicit-only** — never a side effect of resize, delete, or decode.
3. **Mode transitions are boring** — snapshot what must survive, restore what was snapshotted,
   never clear the snapshot you just took.
4. **Shrink `ImageView`** — move session document and layout policy out so mode UI is not
   also the database.

Non-goals for early phases: rewrite Qt widgets, change user-visible features, drop Workspace.

## Ownership extraction roadmap (2026-09-18)

Work in **small tips**: one ownership boundary per tip, behaviour frozen, docs updated.

| Order | Extract | From | Notes |
|------:|---------|------|-------|
| 1 | **GallerySizeResolve** | `ImageView` size-gate timers/pending | Host interface; pack stays on view |
| 2 | **SessionOpen** | `MainWindow` / `mainwindow_session` | beginReplace + prepareExpandedSession |
| 3 | **ProcessMemos** + **SizeProbe** | `thumtoocache.cpp` | memos + serial probe FIFO extracted |
| 4 | **LoadGeneration** + **SessionLoadGate** | `imageview_load` | generation + pending maps |
| 5 | **CropGeometry** + **CropSession** | `imageview_crop` | math + draft state bag |
| 6 | **TileNeighborPrefetch** | prefetch slots | session-replace clear |
| 7 | **SlideshowPhaseState** + Settings/Dwell + **Clocks** | phase + prefs + pure ticks | |

**Rule:** new collaborator types with explicit Host or narrow public API — not more `imageview_*.cpp` slices alone.

### Landed

- **GallerySizeResolve** (`gallerysizeresolve.{h,cpp}`): pending set, 45s safety timer, 50ms memo-sweep progress; `ImageView` implements `GallerySizeResolveHost` for size map / probes / pack-on-complete.
- **SessionOpen** (`sessionopen.{h,cpp}`): `beginReplace` (invalidate + clear filmstrip), `prepareExpandedSession` (second invalidate, appearance seed, stash drop, memo warm, sizesWarm), shared by Open and append chrome.
- **ProcessMemos** (`thumtoo_process_memos.{h,cpp}`): process size memo + durable-tile yes/no/min_scale; session-replace clears durable only.
- **SizeProbe** (`thumtoo_size_probe.cpp`): serial FIFO `scheduleProbe`; Store I/O via `requestSizeAsync`; memo hits still emit `sizeReady`.
- **TileNeighborPrefetch** (`tileneighborprefetch.{h,cpp}`): off-canvas neighbor tile warm; `ImageView` is host; session wipe calls `clear()`.
- **CropGeometry** (`cropgeometry.{h,cpp}`): pure crop-rect constrain/translate/shrink; expand/hit-test/chrome; resize/rotate/rubber-band drag math; no ImageView state.
- **CropSession** (`cropsession.h`): draft rect, target binding, enter-stash, handles; `ImageView::m_crop`; enter/apply still on view.
- **SlideshowPhaseState** + enums (`slideshowtypes.h`): from/to phase buffers, fade/motion clocks, atlas.
- **SlideshowSettings** / **SlideshowDwellState**: prefs bag + dwell atlas / Ken Burns camera.
- **SlideshowZoomBlurState** / **SlideshowProgressHud**: letterbox blur cache + progress/seek/nav-hot HUD clocks.
- **SlideshowClocks**: pure motion T∈[0,1] integration (no QObject); ImageView timers call in.
- **SlideshowPhasePolicy**: pure phase-buffer upgrade rules (edge + ContentXform pending)
  and `acceptOrientedUpgrade` for pool→buffer install.
- **SlideshowAtlasPolicy** + `DwellAtlasParams`: pure atlas coverage, motion headroom,
  target/need edge budgets, `makeParams`, Fit/Fill/Actual `zoomBaseScale`.
- **SlideshowMotionGeometry**: pure cover dest rect, geometric/attention bias paths, aspectMismatch.
- **ZoomBlur helpers**: pure slot book + `makeCover` pixel pipeline; async schedule stays on ImageView.
- **AttentionSession** (`attentionsession.h`): attention draft points + rubber/select gesture state.
- **CentreProgress** / **GroupTransformSession**: centre HUD panel + multi-select scale/rotate gesture.
- **PageGuideSession** / **ItemInteractSession** / **HudFlash**: print guide, single-item drag/rotate, action flash.
- **ViewFraming** / **TextLayerSession** / **ZoomRegionGesture**: sticky zoom, text overlay, Z-rubber zoom.
- **CanvasBackground** / **LayoutPrefs** / **ViewportChrome**: bg/tile, grid columns, pan/hover pointer.
- **HudAppearance** / **SessionIdentity** / **GallerySoftBook**: HUD prefs, session index/id, gallery soft map.
- **LoadGeneration** (`loadgeneration.h`): monotonic token for async decode accept/reject.
- **SessionLoadGate** (`sessionloadgate.h`): generation + pending LoadAdd/LoadRestore/scene maps; `clearPending` / `bumpGeneration`.
- **ImageSizeBook** (`imagesizebook.h`): path→logical size + provisional/probe sets; noteDefinitive HARD RULE.
- **PathItemStateBook** (`pathitemstatebook.h`): path-keyed WorkspaceItemState (placement / unbound).
- **PendingItemAppearanceBook**: Duplicate→bind staged content appearance (ImageItem* keys).
- **SessionBindBook** (`sessionbindbook.h`): LoadAdd PendingSessionBind queue + index/select maps.
- **SessionPathOrder** (`sessionpathorder.h`): session path list + parallel SessionImageId order.
- **SessionAppearanceStore** seed-attempt set (`seedAttempted` / mark / clear with remove).
- **SessionAppearance::mergeAppliedAndLiveFlags**: pure want overlay (applied xform + live flags).
- **SessionAppearance::withoutCrop**: orient-only state (crop fields cleared).
- **SessionAppearance::clearedContentOps**: drop bake ops; keep colour grade / pose.
- **AttentionSession::hasDraftFor**; **SessionAppearance::liveItemHasContentMods**.
- **AttentionGeometry** (`attentiongeometry.{h,cpp}`): pure norm↔local, clamp, translate-selected, handle hit-test, selection set ops.
- **PageGuideGeometry** (`pageguidegeometry.{h,cpp}`): pure 8-handle anchors, hit-test, resize-from-handle.
- **GroupTransformGeometry** (`grouptransformgeometry.{h,cpp}`): pure group scale/rotate handle hit-test, scale factors, rotate delta + orbit.
- **ItemFrameGeometry** (`itemframegeometry.{h,cpp}`): pure rotated-frame view geom + chrome column / opacity-track layout constants.
- **TextLayerGeometry** (`textlayergeometry.{h,cpp}`): pure text-region intersect + reading-order sort.
- **ItemHandlePolicy** (`itemhandlepolicy.{h,cpp}`): pure ImageItem::Handle classification predicates.
- **StackGeometry** (`stackgeometry.{h,cpp}`): pure scene content-overlap + raise/lower z-step math.
- **PerfStats** (`perfstats.h`): BILTOO_PERF paint/FPS + decode-window timings bag.
- **ColorAdjustCommit** / **LayoutDebounce**: pending grade sid/path + debounced pack reason.
- **SelectionGeometry::unionContentAabbs**: pure multi-item content AABB union.
- **GalleryPackFit**: pure pack overshoot targets + uniform scale.
- **PlacementLinear::normalizeDegrees**: shared [0,360) rotation wrap.
- **GalleryRelayoutSuppress**: nested counter for Gallery delete vs resize pack.
- **ViewTransform**: pure scaleFrom (hypot) + padded scene bounds.
- **PlacementLinear::clampScaleXY**; bag clamps on ViewFraming / HudAppearance / SlideshowSettings.
- **LayoutApplyGuard**: pack re-entrancy flag.
- **PlacementLinear** opacity clamp/step; **StackGeometry** zLess/zGreater.
- **ViewTransform::rubberRect**; **SlideshowProgressHud::setCycleProgress01**.
- **GalleryPackReason** in `imageview_types.h` (LayoutDebounce-safe).
- Slideshow bag clamp setters; **ViewTransform::atLeast1**.
- **CropSession::locksPath / locksItem**: draft sample freeze predicates on the session bag.
- **CropSession::isDraftLayoutGeometry**; **SlideshowProgressHud::lastPaintFp**.
- **TileLoadCoordinator** owns PreferCache cancel-for-tiles set (`clearPreferCancelled` on session invalidate).
- **DisplayEdgePolicy**: pure long-edge coverage, ladder native cap, sample-covers-native,
  soft cell clamp, screen longPx → needEdge, QualityTier classify for HUD.
- **SoftDisplayPolicy**: worker LQIP / host-soft underlay selection (no soft encode).
- **EdgeNavPolicy**: pure Image-mode edge chrome hit-test + paint layout (fill/button centre).
- **MotionScrollChrome**: saved scrollbar policies while Ken Burns underlay is active.
- **ZoomBlur::clearAllSlots** + **sessionBadgeAscii** pure session HUD index form.
- **GallerySoftBook** path reset + native-decode set API; SoftDisplayPolicy gallery LQIP gates.
- **SoftDisplayPolicy::aggregatePathHaveEdge**: pure max display edge + any-full for a path.
- **ViewTransform::{uniformFitScale,fitRectCentered}**; **GalleryLayout::axesSwapForItemRotation**.
- **FilmstripGeometry::flowPadFromCellPad**; **SlideshowClocks::intervalLabelParts**.
- **WorkspaceNavGeometry::scoreRelative** for Workspace arrow-key neighbour pick.
- **PlacementLinear** shear/opacity keyboard step helpers; **GalleryPackFit::kDecodeOverscanPx**.
- **CentreProgress::matchesTitlePrefix**; **MotionScrollChrome::release**.
- **ImageSizeBook** provisional stand-in helpers; **GallerySoft** decode-window budgets.
- **LayoutApplyGuard::Scoped**; **ViewFraming::kindFromFitFill**.
- **SessionNavFlags** nav transitions; **TextLayerSession** rubberRect; **PerfStats** slow threshold.
- **AttentionSession::clearSelected**; **CanvasBackground** colour/pattern setters.
- **SlideshowSettings** preference transitions; **GroupTransformSession** pruneNullItems.
- **GallerySoftBook::setDeferPopulate**; **HudAppearance** visible/colour setters.
- **LayoutPrefs::setMode**; **LayoutDebounce::take**; **GalleryRelayoutSuppress::Scoped**.
- **CanvasBackground::setCheckerWorkspaceOnly**; modes Fit/Fill via ViewFraming.
- **Tool** in imageview_types; **ToolPolicy** cursor/rubber-band; **LayoutDebounce::kIntervalMs**.
- **ViewTransform::kFreeformScenePad**; **SlideshowProgressHud** paused/nav transitions.
- **ViewportChrome** left-pan/mouse-info; interaction clear via session bags; **ColorAdjustCommit::kIntervalMs**.
- **SlideshowDwellState** motion flags; **CropSession::setMode**; **PageGuide** visibility + DPI.
- **TextLayerSession::setShowRegions**; **AttentionSession::setMode**; **HudFlash** pulse/action ms.
- **GallerySoft** watchdog/status intervals; **SlideshowProgressHud::kProgressTickMs**; canvas workspace-default.
- **SlideshowProgressHud::kMotionTickMs**; **TileLoadCoordinator::kDefaultTickBudget**; **HudAppearance::kStatusRefreshMs**.
- **TextLayerSession** search fuzzy/query transitions.
- **WorkspaceBackground::matches** / **CanvasBackground::setWorkspace**; **DisplayEdgePolicy::nativeLongEdge**.
- **AttentionSession** rubber via ViewTransform::rubberRect.
- **GallerySoft** decode-window delay tiers; hover cursor via ToolPolicy.
- **SlideshowZoomBlurState::bumpGeneration**; **layoutIsPackaged** / **layoutIsHeightFitted**.
- **CanvasBackground::setPattern**; **CentreProgress::set**; **PathItemStateBook::take**; **ImageMouseInfo::clear**.
- **GalleryPackFit::modeFromLayoutMode**; **GalleryController::clearChrome**; **SessionAppearanceStore::take**.
- **HudFlash::setIdentityPulse** reports change.
- **WorkspaceNavGeometry** int key overload; **GalleryController** focus/hover path transitions.
- **ColorAdjustments::matches**; **layoutIsGridFamily** / **layoutNeedsAllSizes**.
- **ViewFraming** sticky enable/kind; **GalleryController::setPendingRestore**; **ImageSizeBook::take**.
- **ColorAdjustments::fromDurableGrade** for XDG appearance seed.
- **GallerySoftBook::setDeferPopulate**; **SessionIdentity** position/id transitions.
- **GroupTransformSession** / **PageGuideSession** setHoverHandle report change.
- **CropSession::setHoverHandle**; **layoutIsFlowFamily** / **layoutIsMasonryColumns**.
- **SlideshowPhaseState** fade/motion bag transitions.
- **AttentionGeometry::clampNorm** header-inline (link fix); **PageGuideSession::setSelected**.
- **CropSession::setAllowExpand** reports change.
- **HudAppearance::setFontPointSize**; **TextLayerSession::setLinkHoverTip**.
- **ZoomRegionGesture** arm/disarm report change.
- **SessionIdentity::setLastLoadError**; **CropSession::setShowingFullImage**.
- **SlideshowDwellState** motion flags; **PageGuideSession** size/rect; **AttentionSession::hasSelection**.
- **SessionBindBook::takeFront**.
- **groupHoverChanged** fix; **ViewportChrome::setMouseInfo**; **CropSession** setRect/awaiting.
- **SessionPathOrder** isEmpty/size.
- **layoutIsFacing** / **layoutIsSideBySide**; **ViewFraming** fit/fill transitions.
- **CropSession::setRotation**; **SessionBindBook::takeSelectIds**.
- **PageGuideSession** setSelected fix + setPage; **SlideshowSettings** pan-zoom/duration.
- **CropSession::clearAwaitingFull**; **layoutIsVertical**.
- **TextLayerSession** setRubberRect / setSelectedRegions; setLayerContent for load.
- **CropSession** expand min-size via setRect (no direct rect mutation).
- **ItemInteractSession::dropIfItem**; move release via endMove.
- **GroupTransformSession::nullDragItemAt**; destroy via endDrag.
- **AttentionSession::clearDraft** on session-id change; **ViewportChrome::clearMouseInfo**.
- **SlideshowProgressHud** seek/progress/timeline transitions; **SlideshowDwellState**
  applyBias / clearAtlasPixmap / setAtlas / setSourceImage / duration helpers;
  **SlideshowZoomBlurState** clearUnderlays / setViewportSize.
- **ViewFraming** setFitFillFlags / armFit / releaseFit; residual fit/fill routed.
- **SlideshowPhaseState** setFromImage/setToImage / clearToAtlas / stopMotionClocks.
- **SlideshowPhaseState** setFromPath/setToPath / start*MotionClock / promoteFromMotionFromTo /
  ensureFromTiles / ensureToTiles.
- **ZoomRegionGesture** setRubberBand; **TextLayerSession** searchMatches transitions.
- **LayoutPrefs** setMode in setActiveMode; **SlideshowPhaseState** clearRasterQueues /
  bumpPhaseUpgradeGeneration; **ImageView** setHoverEdge / clearHoverEdge;
  clearWorkspace via loadGate clearPending + GallerySoftBook setDeferPopulate.
- **CanvasBackground** setWorkspaceTile; **SessionPathOrder** setOrder;
  **ImageSizeBook** known/contains; **ViewTransform** kEnsureVisibleMargin;
  seekbar near-edge via ProgressHud isSeekHit.
- **PathItemStateBook** get/set/contains (no byPath digs); **SessionBindBook**
  append/index/select helpers; **SessionPathOrder** appendRow.
- **SessionPathOrder** isEmpty/size/pathAt/idAt/pathList/idList; **GallerySoftBook**
  state/get/find; **SessionBindBook** bindCount/bindAt/removeBindAt/takeBindAt.
- **Fix** layoutSizeForPath bookSize binding; **SessionLoadGate** pending scene/
  restore helpers; phase/dwell atlas generation + raster queue helpers.
- ZoomBlur clearUnderlays on resize; Workspace restore via setPendingRestoreStates /
  itemStateForPath; drop ImageView map dig APIs; ProgressHud
  accumulateProgressBaseFromElapsed.
- **Fix** ensureFromTiles/ensureToTiles const; privatize PathItemStateBook /
  GallerySoftBook / ImageSizeBook / SessionBindBook / SessionPathOrder storage;
  pathOrder clear/set for controllers.
- Drop ImageView pathOrderBook/itemStates digs; LoadGate map refs private;
  **TextLayerSession** region accessors; **ZoomRegionGesture** hideRubber/isActive.
- **SlideshowZoomBlurState** slot/lastGood accessors; **GroupTransformSession**
  drag list accessors; **TextLayerSession** matchCount/selectionCount;
  **CropSession** mode checks via active().
- **AttentionSession** active(); **PageGuideSession** isInteractive/isDragging/
  isHandleHot; **ItemInteractSession** isRotating/isHandleDragging;
  **ViewportChrome** isPanning; **CropSession** isHandleHot;
  **TextLayerSession** isRubberbanding.
- **SlideshowProgressHud** progress/nav/paused/seek bool accessors;
  **GallerySoftBook** isDeferPopulate; **ViewFraming** fit/fill/sticky accessors;
  **HudFlash** / **HudAppearance** visibility accessors.
- **SessionNavFlags** nav bool accessors; **SlideshowDwell**/Settings motion
  accessors; **LayoutPrefs** isFreeForm; **SlideshowPhaseState** path/image
  accessors.
- **CropSession** rect/target/rotation helpers; **CanvasBackground** color/tile
  accessors; **GroupTransformSession** isScaleDrag/isRotateDrag;
  **AttentionSession** gesture boolean accessors.
- **SlideshowPhaseState** hasToAtlas/clampedFadeT; **SessionIdentity**
  hasCurrentId/hasLastLoadError; **ImageView** isNavEdge; **CropSession**
  rubber/full/expand accessors; **LayoutPrefs** currentMode.
- **ZoomRegionGesture** rubber/armed helpers; privatize
  **PendingItemAppearanceBook**; **SlideshowPhaseState** contentApplied and
  motion-clock accessors.
- **GroupTransformSession** handle-hot/presence; phase hasFromPath/hasToPath;
  dwell hasBias; **CropSession** handle-drag/hover; **TextLayerSession**
  show/search/selection accessors.
- **Fix** isHandleHot(CropHandle) and CanvasBackground altColor;
  **AttentionSession** hasSelection gates; **SlideshowDwellState** bias/
  duration accessors.
- **ViewFraming** sticky kind/scale; **ViewportChrome** mouse/pan accessors;
  **SlideshowPhaseState** toBias; **CropSession** currentRect/rotation;
  **GroupTransformSession** geometry accessors.
- **SlideshowPhaseState** path/image/motion-T; **ProgressHud**/Settings value
  accessors; **PageGuideSession** visibility/geometry; **ItemInteractSession**
  item/start-state accessors.
- **SlideshowDwell**/phase atlas image accessors; **TextLayerSession** query/
  path/region list accessors.
- Remaining **CropSession** rect/rotation/drag-start digs; **Phase** surface
  and generation accessors; **HudFlash** action/detail; **CanvasBackground**
  workspace/pattern accessors.
- **SessionIdentity** index/total/id/error; **ProgressHud** clock pause;
  ZoomBlur/dwell generation; **PerfStats** enabled/timing accessors.
- **CropSession** enter/stash/hover accessors; **GallerySoftState**
  tilesPyramidQueued; **ViewFraming** sticky pan; **LayoutPrefs** columns.
- **Fix** TextLayer/phase SurfaceId types; **AttentionSession** selected/
  rubber/drag; **CentreProgress** title/detail accessors.
- **SlideshowSettings** letterbox/pad; **CropSession** stashed placement;
  ImageView public wrappers routed through bag accessors.


## Current pain (evidence)

| Symptom | Structural cause |
|---------|------------------|
| Delete repacks Gallery | Pack triggered from resize / decode / debounce |
| Scroll lost Gallery→Image→Gallery | `setViewMode` cleared viewport snapshot |
| Crop wrong only in Image mode | Appearance maps + decode size mismatch; path vs id |
| Suppress counters / singleShot(0) | No pack *policy*; only timing workarounds |

## Target architecture

```
┌─────────────────────────────────────────────────────────┐
│ MainWindow (shell: menus, session list m_files/ids)     │
└─────────────┬───────────────────────────┬───────────────┘
              │                           │
              ▼                           ▼
┌──────────────────────────┐   ┌──────────────────────────┐
│ SessionDocument          │   │ ImageView (presentation) │
│  - ordered ids + paths   │   │  - mode, input, chrome   │
│  - appearance by id      │◄──│  - asks document for     │
│  - allocate / remove id  │   │    appearance on decode  │
└──────────────────────────┘   └────────────┬─────────────┘
                                            │
                               ┌────────────┴────────────┐
                               │ GalleryPackPolicy       │
                               │  pack(reason) only      │
                               └─────────────────────────┘
```

Long-term, `m_files` / `m_sessionIds` move into `SessionDocument` owned by MainWindow
(or a shared model). Early phases keep ownership in MainWindow and only extract
**appearance** + **pack policy** from ImageView.

## Phases

### Phase 1 — Policy and single crop apply path (this work)

**1a. Gallery pack reasons**

```text
enum class GalleryPackReason {
  ExplicitLayout,  // layout toolbar / menu
  EnterGallery,    // enter mode / populate
  Reload,          // F5
  ContentChange,   // rotate/flip that changes tile aspect in pack
  SessionMutate,   // intentional add/duplicate that must show new tiles
};
```

- `applyLayout(GalleryPackReason)` is the only pack entry.
- `scheduleApplyLayout` is removed or becomes a no-op (no resize/decode pack).
- Delete never packs. Resize never packs.

**1b. Session appearance apply helper**

- `sessionappearance.{h,cpp}`: scale crop rect when `cropSourceSize` ≠ live size;
  apply crop + content bakes to an `ImageItem`.
- ImageView calls this from `applySessionCrop` / `applyStoredAppearance` instead of
  duplicating geometry math.

**1c. Document invariants in REFACTOR.md / comments**

- Snapshot before mode leave; do not clear snapshot when destination needs it.
- Appearance identity is `SessionImageId`.

**Exit criteria:** Delete does not move other tiles; F5 / layout still pack; crop apply
code has one implementation.

### Phase 2 — SessionAppearanceStore inside ImageView

- Replace raw `QHash<SessionImageId, WorkspaceItemState> m_sessionAppearance` with a
  small class: `get/set/remove/clear`, `applyTo(ImageItem*)`.
- Stop writing appearance only into `m_itemStates` for bound session images (path map
  remains legacy fallback for unbound tiles only).
- `captureState` / `recordSessionCrop` write through the store.

**Exit criteria:** No feature change; all crop/flip persistence goes through the store API.

### Phase 3 — Mode transition object

- `ModeTransition` or methods `leaveGalleryToImage()`, `returnToGallery()` that own:
  viewport snapshot, stash/restore tiles, prepare canvas, pending restore flag.
- `setViewMode` becomes a thin dispatcher; cannot clear Gallery scroll snapshot on
  Gallery→Image.

**Exit criteria:** Scroll restore and stash logic not scattered across MainWindow +
setViewMode + enterGallery.

### Phase 4 — SessionDocument (MainWindow)

- Move `m_files`, `m_sessionIds`, alloc/remove into `SessionDocument`.
- Signals: `sessionChanged`, `appearanceChanged(id)`.
- ImageView and ThumbnailBar observe the document.

**Exit criteria:** MainWindow session methods become facades; duplicates and delete
update one list.

### Phase 5 — Optional split of ImageView files by mode

- `gallery_controller` / `workspace_controller` as collaborators, not new windows.
- Only after Phases 1–4 stabilize tests and manual QA.

## Rules while refactoring

- No behaviour change without a failing scenario or explicit product decision.
- Prefer delete of dead paths (`scheduleApplyLayout` from resize) over new flags.
- Keep GPLv3+ / REUSE headers on new files.
- Ship as small commits; each phase should be bisectable.

## Progress log

- Phase 1a: `GalleryPackReason` + `applyLayout(reason)`; `scheduleApplyLayout` no-op.
- Phase 1b: `sessionappearance.{h,cpp}` owns crop scale/apply geometry.
- Phase 1c: this document.
- Phase 2: `SessionAppearanceStore` wraps id-keyed appearance; ImageView uses
  `m_appearance.get/set/remove` instead of a raw QHash.
- Phase 3: `leaveGalleryForImage()` / `returnToGalleryFromImage()` own viewport
  snapshot + mode switch so callers cannot forget snapshot or clear it early.
- Phase 4: `SessionDocument` owns ordered paths + session ids; MainWindow
  holds `m_session` and facades alloc/indexOf/remove through it.
- Phase 4b: close SessionDocument mutation API — no mutable paths()/ids();
  MainWindow uses setPaths / replaceAll / append / insert / removeAt / clear
  only so list lengths stay aligned and ids are never reused after remove.
- Session remove undo: SessionEntrySnapshot stores index+path+SessionImageId
  and optional appearance; restore reuses the same id and re-applies appearance.
- Phase 3b: leaveForImageMode() (Gallery+Workspace→Image);
  returnToWorkspaceFromImage() mirrors returnToGalleryFromImage.
- Phase 2b: bound session images no longer last-write appearance
  into m_itemStates (path map); m_appearance is the only content store.
- Phase 2c: remove deprecated index-keyed m_sessionSlotStates; appearance
  is SessionImageId-only. recordSessionCrop writes path map only when unbound.
- MainWindow openSessionIndex/ImageInImageMode consolidates Image entry.
- Gallery focus/remove prefer SessionImageId (sessionImageFocused /
  sessionRemoveIdsRequested); path signals remain unbound fallbacks.
- Removed dead scheduleApplyLayout no-op (pack is explicit-only).
- Phase 5a: Gallery transitions/stash/viewport moved to imageview_gallery.cpp
  (file split by mode; controllers deferred). Removed dead
  invalidateStashedGalleryForSession after crop keeps stash for peer-sync.

## Structural stop line

Phases 1–4 (and follow-ups 2b/2c/3b/4b) are complete.

Phase 5 mode-controller extraction (5a–5u) is complete for the current design:
`setViewMode` dispatches leave/enter to GalleryController, WorkspaceController,
and ImageController; ImageView exposes a public host API (no `friend`);
classic path is owned by ImageController.

Remaining work is either product features (TODO.md), further host-API
narrowing if desired, or Workspace placement by id (new model work — not
part of early-phase exit criteria).
- Phase 5b: Workspace stash/snapshot/free-form placement moved to
  imageview_workspace.cpp (file split by mode).
- Phase 5c: setViewMode / prepare*Canvas / clearLiveCanvas moved to
  imageview_modes.cpp.
- Phase 5d: applyLayout/pack settings → imageview_pack.cpp;
  setWorkspacePaths/add/place/rebind → imageview_canvas.cpp.
- Phase 5e: crop mode + session crop appearance → imageview_crop.cpp.
- Phase 5f: decode/load (createItem, schedule*, onImageLoaded, loadImage)
  → imageview_load.cpp.
- Phase 5g: zoom/HUD/background → imageview_view.cpp;
  flip/rotate/stack/opacity/duplicate → imageview_transform.cpp.
- Phase 5h: paintEvent/draw* → imageview_paint.cpp;
  group scale/rotate → imageview_group.cpp.
- Phase 5i: `GalleryController` collaborator owns Gallery stash, viewport
  snapshot, selection anchor, hover path, and enter/leave/return helpers.
  ImageView public API delegates; no behaviour change.
- Phase 5j: `WorkspaceController` collaborator owns Workspace tile stash,
  durable snapshot, free-form placement cache, and related helpers.
  ImageView public API delegates; no behaviour change.
- Phase 5k: `clearInteractionState()` shared host helper; Workspace enter
  path of `setViewMode` owned by `WorkspaceController::enter`.
- Phase 5l: Gallery leave cleanup → `GalleryController::onLeave`; residual
  `setViewMode(Gallery)` path routes through `GalleryController::enter`
  (MainWindow already uses enterGallery only).
- Phase 5m: Workspace leave → `WorkspaceController::onLeave` (durable
  snapshot; live stash when next mode is Image).
- Phase 5n: Gallery→Image tile stash moved into `GalleryController::onLeave`
  (symmetric with Workspace leave).
- Phase 5o: `ImageController` collaborator owns Image-mode enter
  (prepare canvas, clear live items, reload classic path).
- Phase 5p: controller host helpers on ImageView (`stopDeferredPacking`,
  `setActiveMode`, `takeClassicPath`, `clearPendingLoads`,
  `clearSceneKeepingStashes`, `scheduleReplaceLoad`); Image enter uses them.
- Phase 5q: host helpers `liveItems`, `canvasScene`, `applyModeFlagsToLiveItems`,
  `ensurePrimarySelection`, `applyToolDragMode`; Workspace/Gallery enter use them.
- Phase 5r: stash/restore paths use `liveItems`/`canvasScene`/`pathOrder`;
  `clearFitFillModes` for free-form restore.
- Phase 5s: appearance/itemStates/pending-load host accessors; controllers
  no longer touch those private fields by name.
- Phase 5t: promote controller-used operations to public host API
  (`applyItemModeFlags`, `findItemByPath`, `destroyCanvasItem`,
  `scheduleRestoreLoad`, snapshot/stash wrappers, …); drop `friend`.
- Phase 5u: `m_classicPath` owned by `ImageController`; ImageView exposes
  classicPath/hasClassicPath/setClassicPath/clearClassicPath/takeClassicPath.
- Phase 5v: document mode-controller host API banner on ImageView; update
  structural stop line to mark Phase 5 controller work complete.

### Post–Phase 5 (product polish, same session)

- Duplicate path tiles are first-class (occurrence + SessionImageId); Gallery
  remove repacks without holes; history open no longer double-destroys items.
- Undo: content bake (flip/rotate), geometry (raise/lower/opacity/reset),
  session remove, Duplicate (SessionDuplicateCommand).
- Menus: content transforms and session sort under Edit; View is display/chrome.
- Edge H/V scale handles match corner Ctrl semantics (default opposite edge).
- Project files (`.biltoo` JSON + SHA-256 assets), Export PNG, fit page guide
  to content, missing-asset relink on load.
- Workspace thumbnail membership and path-map appearance use SessionImageId
  only (no path-occurrence hide / cross-duplicate flip leakage).
- **ContentXform** aspectRatio / aspectChanged / footprintScaleFactor; HUD setTimelineProgress.
- **ViewFraming** aspectMode + sticky pan pure; **ViewTransform::unitFraction**; **SlideshowClocks::progress01**.
- **HudGeometry** placePanel; **LayoutPrefs** clamps; cover scale + roundedSizeAtLeast1; sanitizeDwellDurationMs.
- **TextSearchPolicy**; **CanvasPatternGeometry**; **HudGeometry::wrapHudLine**.
- **ViewTransform** significantRubber/chebyshev/sanitizeViewScale; **ContentXform::invAxisScale**; **CanvasBackground** checker; **TextLayerSession::needsLayer**.
- Stack 1338–1339 pure helpers; **GalleryPackFit::packAvailAxis**; **GallerySoft::maxHave**.
- **DisplayEdgePolicy** climb/bind/tileSynth; crop font clamps; materializePreviewEdge.
- **maxAxisScale**; **clampGroupScaleAxis**; **safeDivisor**; **SlideshowClocks** policy clamps; track clamp01; pairCount.
- **WorkspaceGeometry** margins/footprint; HUD maxPanel widths; clampPanZoomFactor; layout longEdge.
- **ItemFrameGeometry** opacity track min/max/span + trackParamFromOpacity; **SlideshowClocks::clampStoredIntervalMs**; **HudGeometry** progressFillWidth / progressBarHeight.
- **ColorAdjustments** grade clamps / gamma percent; **FilmstripGeometry**; **ViewTransform::overlayFontPixelSize**; **DisplayQuality** debug stamp metrics.
- **ItemFrameGeometry** chrome/placeholder fonts; **AttentionGeometry::clampHintPointSize**; **ImageCache::rgbaCostKiB**; **ViewTransform** pointAlong / clampedProgress.
- **GalleryLayout** pack cell/scale helpers; **SlideshowMotionGeometry** bias/panZoom clamps; **SlideshowAtlasPolicy::clampPanZoomHeadroom**.
- **SlideshowClocks::formatClockMs**; **ContentXform** heightForAspectWidth / roundedSize(QSizeF); **ViewTransform::nonNegMs(qint64)**.
- **Fix AttentionGeometry** header; detect/trim clamps; **DisplayQuality::kOverviewMaxEdge**; **EdgeNavPolicy** zone constants.
- **ViewTransform** containScale / coverScale / floorScale; **ItemFrameGeometry** selectedChromePad / maxChromeOffset.
- **PlacementLinear** clampShear in decompose; **CropGeometry** kChrome*; **ViewTransform** clampIndex/Pixel; SoftDisplay LQIP overloads.
- Group snapDegrees; clocks clamp01; **WorkspaceGeometry** sceneMargins/paddedSceneRect; **HudGeometry::placeTimelineClock**.
- **Fix DisplayQuality::hostLongEdge**; **ContentXform::scaleCropRect**; ViewFraming clamp01.
- **PathRasterService::capWant** → DisplayEdgePolicy; **CropGeometry::kFreeRotationEps**; tile atLeast1.
- **ColorAdjustments::scopeSampleStep**; **ViewTransform::nonNeg** for transition caps.
- clamp01 attention/motion/project; **clampInsertIndex**; filmstrip atLeast1; luma clampPixel.
- **SlideshowClocks** intervalFaster/Slower; ZoomBlur coverScale/clampPixel; main/workspace atLeast1.
- **ZoomBlur::workSize**; **ViewTransform::nonNeg(int)** for progress/margins/budget.
- **ContentXform::clampEstimatedEdge**; **ColorAdjustments::clampUnit**; residual nonNeg/atLeast1.
- **FilmstripGeometry** virtualOverscan/seedVirtualRange; DisplayEdge/ImageCache atLeast1.
- **ViewTransform** axisAlignedHandlePoints; **AttentionSession** rubber begin/update/end.
- **TextLayerSession** rubber transitions; **PageGuideSession** resize/hover transitions.
- **CropSession** handle/rubber/hover; **GroupTransformSession** begin/end drag + hover.
- **CropSession** enter bind/snapshot/stash/abort; **AttentionSession** setDraft/enterMode/leaveMode.
- **CropSession** leave via clear/takePending; **TextLayerSession** showRegions/link tip.
- **AttentionSession** point-drag/selection; **PageGuideSession** visibility/page; group press pos.
- **ItemInteractSession** move/handle/rotate; Crop setRect/setRotation; TextLayer layer content.
- **ViewportChrome** pan/pointer transitions; Crop residual draft seeds.
- **Fix nonNeg(qint64)** for qsizetype; **TextLayerSession** setSearchQuery; **HudFlash** show/pulse.
- **ZoomRegionGesture** arm/drag; **ViewFraming** fit/fill/sticky; **SessionIdentity** position/error.
## Phase 6 — ImageView behaviour extraction (proposed)

Phases 1–5 extracted **pure helpers** and **state bags**. They did not extract
**behaviour**: the bags became `ImageView` members and the methods that drive
them stayed on the view. This phase moves behaviour together with the state it
owns, so `ImageView` becomes the shell AGENTS.md already describes — scene,
mode dispatch, input router, paint sequencer — and nothing else.

Follows DOMAIN.md / IDENTITY.md / SESSION.md. No new domain model.

### Evidence (measured at 8427e69)

| Metric | Value |
|--------|------:|
| `imageview.h` + `imageview_types.h` + 21 `imageview*.cpp` | 21,043 lines |
| `imageview.h` alone | 1,924 lines |
| `ImageView::` member functions | 642 |
| Public declarations | 455 |
| …never referenced outside `imageview*` | **194** |
| Data members | ~65 |
| Test binaries touching `ImageView` | **0** |

Member/file coupling — the file split is orthogonal to the data:

| Member | Translation units touching it |
|--------|------------------------------:|
| `m_scene`, `m_items` | 12 each |
| `m_crop` | 11 |
| `m_framing`, `m_gallery` | 10 |
| `m_sessionId`, `m_appearance` | 9 |

Only 18 of ~65 members are touched by exactly one slice. Splitting a class
across translation units without splitting ownership is why 194 helpers are
public: the slices need to see each other, so the header carries the whole
internal vocabulary.

Two specific findings:

- `imageview_view.cpp` (3,390 lines) is **65% slideshow** by line count, with
  HUD text and zoom/framing stapled on. Three unrelated subsystems, one file.
- `ImageView::m_pathOrderBook` (`SessionPathOrder`: paths + parallel
  `SessionImageId`s) is a **second copy of `SessionDocument`'s data**, and
  `m_appearance` still lives on the view. The Target architecture diagram above
  shows appearance owned by `SessionDocument`; that diagram is not yet true.

### Extraction ladder

Cohesion order. One tip per boundary, behaviour frozen, this document updated.

| Tier | Extract | From | Approx. lines |
|-----:|---------|------|--------------:|
| 0 | Header closure (no code moves) | `imageview.h` | — |
| 1 | **SlideshowController** | `imageview_view` + paint/input/load/modes | ~2,900 |
| 2 | **CropController**, **AttentionController** | `imageview_crop*`, `imageview_attention` | ~1,200 |
| 3 | **HudModel** + pure formatting | `imageview_view`, `imageview_paint` | ~800 |
| 4 | Appearance into **SessionDocument** | `imageview_layout`, `imageview_appearance` | ~1,200 |
| 5 | **DisplayPipeline** | `imageview_load` | ~3,000 |
| 6 | Input router dispatch list | `imageview_input` | residual |

**Rule (unchanged from Phase 5):** new collaborator types with an explicit Host
or narrow public API — not more `imageview_*.cpp` slices alone. New collaborators
follow the `GallerySizeResolveHost` pattern, not `friend`.

### Tier 0 — Header closure

Privatize the 194 public methods that no file outside `imageview*` references.
Keep a documented `ImageViewHost` banner for what the mode controllers genuinely
need. Mechanical, compiler-checked, no behaviour risk; do it first so every later
tier is measured against an honest API surface.

**Exit criteria:** `imageview.h` under 1,000 lines; public method count under 260;
no new `friend`.

### Tier 1 — SlideshowController

Biggest and cleanest win. Every pure policy it needs already exists
(`SlideshowClocks`, `SlideshowPhasePolicy`, `SlideshowAtlasPolicy`,
`SlideshowMotionGeometry`, `zoomblurhelpers`), so this tier moves orchestration
only.

Owns: `m_ss`, `m_ssDwell`, `m_ssHud`, `m_ssSettings`, `m_ssZoomBlur`,
`m_motionScroll`, `m_motionTimer`, `m_slideshowProgressTimer`, phase
`DisplaySurface::SurfaceId`s.

`SlideshowHost` surface (keep it this small):

```text
currentPath()            viewportWidgetSize()      devicePixelRatio()
requestRepaint()         applyFraming(...)         requestRaster(path, edge)
slideshowRaster(path)    sessionBadgeText()
```

Integration points:

- Paint: `imageview_paint.cpp` calls `paintUnderlay(QPainter&)` and
  `paintPhase(QPainter&)`; no slideshow state read from the view.
- Input: seek-bar and centre-click become `tryMousePressSlideshow*` returning
  `bool`, matching the existing `try*` router convention.
- GUI-thread rules per GUI_THREAD_AUDIT.md are unchanged — the controller owns
  the timers but still runs on the GUI thread.

**Exit criteria:** `imageview_view.cpp` deleted or under 900 lines; no `m_ss*`
member on `ImageView`; slideshow transitions, Ken Burns motion, ZoomBlur
letterbox, and seek behave identically by manual QA.

### Tier 2 — CropController and AttentionController

Both already have pure geometry (`CropGeometry`, `AttentionGeometry`) and a
session bag (`CropSession`, `AttentionSession`); only enter/apply/leave
orchestration is on the view. The crop slices touch just 9 members.

Host surface: target item, undo stack push, repaint, raster request, relayout
after leave. `CropAppearanceCommand` keeps its current undo semantics; the
`friend class CropAppearanceCommand` declaration goes away with the move.

**Exit criteria:** `friend` list on `ImageView` empty; crop enter/apply/cancel
and attention point edit unchanged in all three modes.

### Tier 3 — HudModel

`statusText*`, `hudFileName`, `pixelQualityLabel`, `loadingStatusHudLine`,
`slideshowPrefetchHudLine`, `appendThumtooDebugStatus` are string formatting over
view state. Introduce a `HudModel` snapshot struct plus pure formatting
functions; `HudGeometry` already owns placement.

This is the first piece of `ImageView` behaviour that becomes unit-testable
without a `QGraphicsView` — add `tests/hudmodel_test.cpp` in the same tip.

**Exit criteria:** status/HUD text produced by pure functions over a snapshot;
at least one test binary covering quality-tier and session-badge formatting.

### Tier 4 — Appearance into SessionDocument

Finishes Phase 4 and makes the Target architecture diagram accurate.

- Move `SessionAppearanceStore m_appearance` into `SessionDocument`.
- Delete `m_pathOrderBook`; the view queries the document for path/id order.
- `ImageView` asks the document for appearance on decode, per the diagram.
- `appearanceChanged(SessionImageId)` drives ThumbnailBar and filmstrip.

Identity stays `SessionImageId` (IDENTITY.md). The path map
(`PathItemStateBook`) remains Workspace free-placement cache and unbound
fallback only — that boundary was set in Phase 2b/2c and does not change.

This is the highest correctness payoff of the ladder: the duplicate-path and
crop-identity entries in *Current pain* all trace to two session models.

**Prerequisite:** characterization tests (see Rules below). Do not start Tier 4
without them.

**Exit criteria:** one session model; `git grep m_pathOrderBook` empty;
duplicate paths, Gallery remove, and crop-in-Image-mode unchanged.

### Tier 5 — DisplayPipeline

`imageview_load.cpp`: load generations, `DisplaySurface` binding, PreferCache
climbs, tile LOD, host rematerialize. Genuinely entangled with mode and
framing — leave it last, when the Host pattern has been exercised four times.

Owns: `m_loadGate`, `m_displaySurfaces`, `m_imageFocusSurface`,
`m_imageModeSoftProvider`, `m_tileCoordinator`, `m_tileLodTimer`,
`m_tileLodZoomDebounce`, `m_tileNeighborPrefetch`.

SIZE.md's HARD RULE (soft sample dimensions never define logical size) moves
with `ImageSizeBook` and must be restated in the new header.

**Exit criteria:** `imageview_load.cpp` under 800 lines; no regression in
soft→full climb, hard reload (Shift+F5), or Gallery blank-cell recovery.

### Tier 6 — Input router

`imageview_input.cpp` is already a clean `try*` router. After Tiers 1–2 most
handlers have moved into their controllers; what remains becomes a dispatch
list over registered handlers. Transform chrome input stays owned by
`ImageView` (AGENTS.md sharp edge) — HANDLES.md is normative here.

### Rules while refactoring

Phase 1–5 rules still apply. Additions:

- **Per extraction:** (1) define the Host with only what compiles, (2) move
  state, (3) move methods unchanged, (4) delete forwarding wrappers,
  (5) privatize the residue. Steps 1–3 must be reviewable as pure moves.
- **Tiers 0–3 are safe as mechanical moves. Tiers 4–5 are not.** Add
  characterization tests before Tier 4: an offscreen `QTest` harness that drives
  `ImageView` through open → Gallery → crop → return → Image and asserts
  appearance, logical size, and framing. Today no test touches `ImageView` at
  all, so these tiers are otherwise unbisectable.
- No behaviour change without a failing scenario or explicit product decision.
- GPLv3+ / REUSE headers on new sources.

### Exit criteria (whole phase)

| Target | Value |
|--------|------:|
| `imageview.h` | < 400 lines |
| Public methods | < 150 |
| Members touched by > 3 translation units | 0 |
| `imageview_view.cpp` | gone |
| Largest remaining `imageview_*.cpp` | < 800 lines |
| `ImageView` total | ~3,000–4,000 lines |

### Note on the structural stop line

The stop line above states that remaining work is product features or "further
host-API narrowing if desired." Tiers 0–3 and 6 are that narrowing. Tier 4 is
not optional narrowing — two session models is a correctness issue, and the
Target architecture diagram documents a state the code has not reached. Amend
the stop line when Tier 4 lands.

### Progress log (Phase 6)

- Tier 0: **done** (biltoo-1596) — privatized 190 methods with no refs outside
  `imageview*`; declarations live in `imageview_private_methods.inc` +
  `imageview_private_rest.inc` included from `private:`. Metrics: `imageview.h`
  993 lines; public methods 259; no new `friend`.
- Tier 1a: **done** (biltoo-1597) — state ownership on `SlideshowController`.
- Tier 1b: **done** (biltoo-1598) — 95 orchestration methods moved to
  `SlideshowController` (QObject + friend of ImageView for private host access).
  ImageView public slideshow API is thin forwards. `imageview_view.cpp` 853 lines
  (status/HUD temporarily in `imageview_status.cpp` pending Tier 3 HudModel).
  Narrow `SlideshowHost` (replace friend) still open as 1c / follow-up.
- Tier 1: **done** for exit metrics (view.cpp ≤900, no m_ss* on ImageView);
  Host-surface cleanup remains (`friend SlideshowController`).
- Tier 2a: **done** (biltoo-1600) — `CropController` / `AttentionController` own
  `CropSession` / `AttentionSession`; removed unused `friend CropAppearanceCommand`
  (`applyCropAppearance` is already public). Method move + empty friend list = 2b.
- Tier 2: _in progress_ (2a state ownership)
- Tier 3: _pending_
- Tier 4: _pending_
- Tier 5: _pending_
- Tier 6: _pending_
