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
- ~~`ImageView::m_pathOrderBook`~~ was a **second copy of `SessionDocument`'s
  data** (fixed: `m_pathOrderOverlay`, tips 1883–1884; Explicit empty still
  dual-model for mode-leave / LoadAdd). Appearance now lives on
  `SessionDocument` (Tier 4b). Full ImageView harness still open before
  document-only pack is trusted.

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
- Pack order lives on `PackOrderOverlay` (Explicit); optional FollowDocument collapse later. The view must not pack from SessionDocument alone until the ImageView harness is green.
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


### Tier 4 residual characterization (2026-09-20 / tips 1864–1887)

`ImageView::m_pathOrderBook` is **gone** (replaced by `m_pathOrderOverlay`,
1883). Remaining gate is **semantic**: do not pack from SessionDocument alone
until the full offscreen ImageView harness is green.

| Concern | Owner today | Notes |
|---------|-------------|-------|
| Session path list + ids | `SessionDocument` (`MainWindow::m_session`) | Source of truth for open files |
| Gallery pack / LoadAdd multiplicity | `m_pathOrderOverlay` on `ImageView` | `pathOrderOccurrences` must **not** consult the document when Explicit empty (blank Workspace would recreate session tiles — see `imageview_load.cpp`) |
| Gallery leave/enter stash | `GalleryController::m_stashedPackOrder` | Snapshot of pack order, not the document |
| Ad-hoc Workspace place | `pathOrderAppendRow` from canvas place | Rows may exist with invalid session id |

**Host mutators (tip 1867 / 1883–1884):** `pathOrderClear` / `pathOrderSetOrder` /
`pathOrderAppendRow` / `currentPackOrder` / `pathOrderOccurrences` /
`pathOrderIsEmpty` live on the public host pipeline (overlay Explicit;
setOrder may collapse when aligned). `setPathOrderFromLiveItems()` rebuilds
from live canvas tiles.

**Bound document:** `bindSessionDocument` routes `firstSessionIdForPath` to the
document when bound; pack multiplicity stays on the overlay. Appearance store
migration (Tier 4b) is done separately.

#### Call-site inventory (tip 1871; storage = overlay since 1883)

| Site | API | Role | Overlay-only? |
|------|-----|------|---------------|
| `gallerycontroller` stash/restore | `currentPackOrder` / `pathOrderSetOrder` | Leave/enter Gallery | Yes — stash is pack order |
| `gallerycontroller` applyLayout / ensurePlaceholders | `pathOrderIsEmpty` / `currentPackOrder` | Reorder + pack order | Yes — may include multiplicity |
| `gallerycontroller` leave | `pathOrderClear` | Explicit empty without wiping session | Yes |
| `gallerycontroller` layout switch | `GalleryController::setPathOrderFromLiveItems` (private; uses `liveItems` + `pathOrderSetOrder`) | Resync from tiles | Yes — tip 1872 moved off ImageView |
| `workspacecontroller` leave | `pathOrderClear` | Same | Yes |
| `displaypipelinecontroller_load` completeLoadAdd | `pathOrderOccurrences` | How many tiles for path | **Must** be overlay |
| `displaypipelinecontroller_load` reorder | `currentPackOrder` | Order live items | Yes |
| `imageview_canvas` / `canvas_place` | `pathOrderSetOrder` / `AppendRow` / `currentPackOrder` | Session place + ad-hoc | Yes — invalid ids OK |
| `imageview_modes` enter transitions | `pathOrderClear` | Mode switch | Yes |
| `imageview_session_remove` | `currentPackOrder` / `pathOrderSetOrder` | Prune pack rows | Yes |
| `imageview_size_book` sizeResolvePathOrder | `currentPackOrder` | Probe order | Yes |
| `imageview_load` pathOrderOccurrences | overlay count | LoadAdd multiplicity | **Must** be overlay |
| `firstSessionIdForPath` | doc then overlay | Identity lookup | Doc preferred |

`PackOrderView::fromDocument()` is valid only when pack **aligns** with the
document (`alignsWithDocument`) **and** there are no ad-hoc Workspace rows
(invalid session ids / extra multiplicity). Gallery pack reads
`currentPackOrder()` → overlay resolve (Explicit or FollowDocument when
collapsed). Do not use SessionDocument alone for pack while Explicit empty /
LoadAdd multiplicity still matter.

**Policy type (tip 1873, removed 1887):** former `PackOrderReadSource` +
`packOrderForRead()` — pack reads now only via overlay resolve.

**PackOrderOverlay (tip 1881):** design type in `src/packorderoverlay.h` with
FollowDocument vs Explicit modes. Explicit empty models `pathOrderClear` (pack
blank while document membership remains). Pure tests:
`tests/packorderoverlay_test.cpp`. Normative write-up: [docs/PATH_ORDER.md](docs/PATH_ORDER.md)
§ PackOrderOverlay. ImageView stores `m_pathOrderOverlay` (1883+); pure harness
green; full ImageView decode harness still pending.

**Migration (do not skip):** (1) design type + pure tests — done 1881;
(2) adopt storage — done 1883; (3) optional FollowDocument collapse — done 1884;
(4) pure characterization / dual-model — done 1885–1886; (5) dead read-source
API removed — 1887; (6) **ImageView harness green** (decode + framing) — still
open. Member `m_pathOrderBook` already gone under `src/`.

**Safe next steps:** verify `-DBILTOO_IMAGEVIEW_CHARACTERIZATION=ON` builds
and green on a Qt host (1891–1892). Then optional decode/framing assertions.
Do not pack from SessionDocument alone until that config is trusted.


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


### Remaining Phase 6 work (post-1613)

**Compile green** as of biltoo-1622 (controller stack links and runs).

**Done (mechanical):** Tiers 0–3, 2a/2b, 5a state ownership, 6a/6b mode input
dispatch, HudModel + session identity characterization tests. Host surface
split (1913–1915): `host_accessors` bags, `host_ops` / `host_crop_display` /
`host_pipeline` operations. One intentional friend:
`ImageViewTransformGeometryCommand` (geometry undo; biltoo-1911).

**Still open:**
1. **Tier 4 residual** — Appearance on `SessionDocument` (Tier 4b). Pack order
   on `PackOrderOverlay` (1883–1884). Pure + dual-model (1885–1886); dead
   read-source API (1887). `biltoo_lib` + optional characterization link
   (1891). Offscreen ImageView body exercises pack/crop/LoadAdd without
   decode wait (1892). **Pure scaffold green** (1921: 16 pass / 1 skip on Qt 6.11). **Still open:**
   full harness decode/framing; green ctest with
   `-DBILTOO_IMAGEVIEW_CHARACTERIZATION=ON` (needs RAM for biltoo_lib).
   See PATH_ORDER.md / IMAGEVIEW_CHARACTERIZATION.md.
2. **Tier 5** — **done** for exit size: PreferCache/install/schedule/tile LOD on
   `DisplayPipelineController` (split TUs + jobs). Soft provider and neighbor
   prefetch stay on ImageView. ImageView→pipeline thin-forward TU removed
   (biltoo-1835–1836); `imageview_load.cpp` deleted (1895); load/path helpers in
   canvas / size_book / pipeline TUs.
3. **Tier 6 remainder** — input try* hop demoted (biltoo-1837: dispatch calls
   controllers directly; `imageview_input_forwards.cpp` gone). Transform chrome
   stays on ImageView (AGENTS.md). Paint/input try* decls private (1910).
4. **Metrics** — `imageview.h` ~598 lines;
   ~26 `imageview*.cpp` TUs,
   ~10470 lines total (down from ~21k / many more TUs at Phase 6 start).
   1894–1908 folded thin/grab-bag TUs by domain; 1910–1915 host-surface
   narrowing. Further merges have weak cohesion payoff; prefer green harness.
5. **Beyond Phase 6** — per-id component ownership and demoting `ImageItem` from
   a parallel appearance store are **Phase 7** (ItemWorld), not more Phase 6
   controller extraction. See Phase 7 below.

### Progress log (Phase 6)

- biltoo-1676..1693 (base 789df2d): mode reload ownership, layout dissolve, paint/view/input/canvas TU splits, SessionPathOrder characterization expand. Largest ImageView TUs now ~950 lines (canvas/appearance).
- biltoo-1694..1702: further TU splits (workspace chrome, edge/dnd, crop/bake, appearance commit); include/link fixes after layout dissolve; dual-model path-order characterization. Appearance ~435 + commit ~393.
- biltoo-1703: canvas focus/destroy + color-grade TUs (canvas ~485, rematerialize ~329).
- biltoo-1704: size book/GallerySizeResolve host + transform actions (imageview.cpp ~703, transform helpers ~146).
- biltoo-1705: session remove/bind split + image-mode framing TU (session_bind ~242, framing ~208).
- biltoo-1706: input event routers, paint background, shell double-click/leave (input ~228, paint ~224, imageview.cpp ~573).
- biltoo-1707: accessors + clipboard TUs; imageview.cpp ~408 (ctor/dtor). Pure slice series complete — characterization/Tier 4 next.
- biltoo-1708..1709: split link fixes; dual-model characterization expand + IMAGEVIEW_CHARACTERIZATION.md harness plan.
- biltoo-1710: PackOrderView pure snapshot (fromBook/fromDocument) for eventual pack source swap.
- biltoo-1711: currentPackOrder host + ensurePlaceholders walks PackOrderView; accessors ImageCache include.
- biltoo-1712: gallery reorder, session_remove, size_book, canvas walk currentPackOrder().
- biltoo-1713: pathOrderOccurrences + gallery stash reorder via PackOrderView; firstIdForPath on view.
- biltoo-1714: LoadAdd reorder, canvas_place id checks, firstSessionId fallback via currentPackOrder().
- biltoo-1715: host IsEmpty/Size/PathAt/IdAt via currentPackOrder(); PATH_ORDER read-path note.
- biltoo-1716: session-gallery-crop-scenario pure narrative; REFACTOR Tier 4 residual clarified.
- biltoo-1717: Gallery stash PackOrderView (paths∥ids); pathOrder() by value.
- biltoo-1718: Gallery layout-switch pack rebuild preserves session ids.
- biltoo-1719: remove paths-only setPathOrder; PackOrderView overload.
- biltoo-1720: setPathOrderFromLiveItems(); Gallery layout-switch uses it.
- biltoo-1721: PackOrderView locals — fix dangling ref warnings on .ids()/.paths().

- Tier 0: **done** (biltoo-1596) — privatized 190 methods with no refs outside
  `imageview*`; declarations live in `imageview_private_methods.inc` +
  `imageview_private_rest.inc` included from `private:`. Metrics: `imageview.h`
  993 lines; public methods 259; no new `friend`.
- Tier 1a: **done** (biltoo-1597) — state ownership on `SlideshowController`.
- Tier 1b: **done** (biltoo-1598) — 95 orchestration methods moved to
  `SlideshowController` (QObject). ImageView public slideshow API is thin
  forwards. Host accessors replaced friend (1605). HudModel in Tier 3.
- Tier 1: **done** for exit metrics (view.cpp ≤900, no m_ss* on ImageView);
  Host surface landed in biltoo-1605 (no friend).
- Tier 2a: **done** (biltoo-1600) — `CropController` / `AttentionController` own
  `CropSession` / `AttentionSession`; removed unused `friend CropAppearanceCommand`
  (`applyCropAppearance` is already public). Method move + empty friend list = 2b.
- Tier 2 host: **done** (biltoo-1605) — Slideshow host accessors; `friend` list empty.
- Tier 2b attention: **done** (biltoo-1606) — methods on `AttentionController`.
- Tier 2b crop: **done** (biltoo-1607) — methods on `CropController`.
- Tier 3: **done** (biltoo-1604) — `HudModel` pure formatters + `tests/hudmodel_test.cpp`.
- Tier 4 prerequisite: **started** (biltoo-1608) — `sessiondocument_test` +
  `sessionappearance_test` lock id-keyed appearance and path/id identity.
  Full offscreen ImageView open→Gallery→crop harness still open.
- Tier 4a: **done** (biltoo-1623) — `SessionDocument` owns a `SessionAppearanceStore`
  (additive; ImageView still has live store). Char test covers doc-keyed crop.
- Tier 4b: **done** (biltoo-1626) — view-owned `m_appearanceOwned` removed; `appearance()` requires `bindSessionAppearance`. `m_pathOrderOverlay` on view (Explicit; former book).
- Tier 4 path-order: **in progress** (biltoo-1630) — LoadAdd multiplicity is view-book only; document for firstId identity only.
  Documented dual model in `docs/PATH_ORDER.md` + `sessionpathorder` unit test (biltoo-1663).
  MainWindow binds at construct; `SessionDocument::clear` clears appearance.
  View keeps fallback owned store when unbound. `m_pathOrderOverlay` on view (Explicit; former book).
- Tier 5a: **done** (biltoo-1610) — `DisplayPipelineController` owns loadGate,
  displaySurfaces, imageFocusSurface, tileCoordinator, tile LOD timers.
  Methods stay on ImageView; soft provider + neighbor prefetch remain on view.
- Tier 5b: **exit size met** (biltoo-1646); Tier 5c: **friend removed** (biltoo-1648); controller split into core/load/item TUs (biltoo-1649); shared jobs (1650); compile fixes (1651–1654).
- Tier 5d: **tile LOD pump on controller** (biltoo-1655) — `tickPrimaryTileLod` / schedule / purge / dropAll; `imageview_load.cpp` ~537 lines.
- Tier 5e: **forwards TU** (biltoo-1656) — thin pipeline forwards in `imageview_pipeline_forwards.cpp`; load.cpp residual only.
- Tier 6a: **done** (biltoo-1612) — crop/attention mouse try* handlers on
  controllers; ImageView input is thin dispatch for those modes.
- Tier 6b: **done** (biltoo-1613) — crop/attention release+key; slideshow seek
  on controllers.
- Tier 6: _mostly done_ (workspace/transform chrome try* remain on view;
  transform chrome is intentionally ImageView-owned per AGENTS.md).
- Tier 6c: **done** (biltoo-1657) — Gallery hover/wheel/press try* on GalleryController.
- Tier 6d: **done** (biltoo-1658) — Workspace Select press on WorkspaceController; chrome/rotate remain view-owned.
- Tier 6e: **done** (biltoo-1660) — Gallery key nav (arrows/Home/End/Enter) on GalleryController.
- Tier 6f: **done** (biltoo-1661) — thin mode-controller input forwards in `imageview_input_forwards.cpp`.
- Tier 6g: **done** (biltoo-1662) — Delete/Backspace selection on Gallery/Workspace controllers.
- Tier 6h: **done** (biltoo-1665) — Image session nav keys + Workspace shear keys on controllers.
- Tier 6i: **done** (biltoo-1666) — Image edge chrome press on ImageController; more input forwards TU.

## Phase 7 — ItemWorld / data-oriented components (proposed)

Phase 6 largely landed: controllers own behaviour, pure geometry helpers exist,
characterization tests expanded (14 CTest binaries including dual-model and
session-gallery-crop-scenario). Measured at **0c4c246** (tip 1721):

| Metric | Phase 6 start (8427e69) | Now (0c4c246) |
|--------|------------------------:|--------------:|
| `imageview*.cpp` lines | ~21,043 | ~12,077 |
| `imageview.h` | 1,924 | ~931 |
| Controllers | none | Slideshow / Crop / Attention / DisplayPipeline / Gallery / Workspace / Image |
| Test binaries | 7 | 14 |

That changes what “go ECS” means. The tree is already **~70% ECS-shaped by
accident** — not Bevy, not a rewrite. Data-oriented here means: **one owner per
fact**, flat keyed tables, stateless transforms. Pitch is **single source of
truth**, not cache locality or SoA speed.

### Already present (map to ECS vocabulary)

| ECS concept | What exists today |
|-------------|-------------------|
| **Entity id** | `SessionImageId` — normative per IDENTITY.md |
| **Component stores** | `SessionAppearanceStore` (`QHash<SessionImageId, WorkspaceItemState>`), `PathItemStateBook`, `ImageSizeBook`, `GallerySoftBook`, `PendingItemAppearanceBook`, DisplaySurface bindings |
| **Systems** | `ContentXform`, `CropGeometry`, `GalleryLayout`, `AttentionGeometry`, `PlacementLinear`, `ItemFrameGeometry`, `HudModel` — largely stateless functions over data |

### The two structural problems left

1. **`WorkspaceItemState` is a fat god-component.** One struct holds identity,
   Workspace pose, content bake flags, crop, attention, and color. Optional
   sub-records use bool flags (`hasCrop`, `hasAttention`) plus duplicated
   flip fields (`hFlip` / `contentHFlip`). Optional-by-bool inside a fat
   struct is exactly what component **presence** replaces. `syncAttentionPrimary()`
   exists only to keep primary and list copies aligned inside the same struct.

2. **The same per-item facts live in three places.**
   - `SessionAppearanceStore` keyed by id
   - `PathItemStateBook` keyed by path (Workspace unbound / placement cache)
   - `ImageItem` members (scale, shear, rotation, session crop rect, color,
     content flips, press-anchor scratch, tile-LOD cache, …)

   Gluing those copies is why `captureState` appears widely (dozens of call
   sites across appearance, bake, crop, transform, workspace chrome, …). The
   *Current pain* rows for duplicate paths, crop identity, and gallery reorder
   losing ids are **sync bugs between these copies** — the same class of bug
   Phase 6 Tier 4 residual (pack overlay vs `SessionDocument` dual-model) addresses
   for pack order.

### What this phase is not

- **Do not replace `QGraphicsScene`.** It owns hit-testing, z-order, viewport
  culling, and repaint regions. Replacing it is a product rewrite, not a cleanup.
- **Do not build a system scheduler.** Qt is event-driven; there is no frame
  tick to schedule against. Systems fire from signals, timers, and input — that
  stays.
- **Do not introduce archetype / chunk / SoA storage for performance.** At a
  few thousand items, AoS→SoA is not the bottleneck; decode and tile LOD are.
  Stage 5 (storage) is last, or never, and only if measured.

### Stages

Cohesion order. One tip (or small tip series) per boundary; behaviour frozen;
characterization extended before storage changes.

| Stage | Work | Notes |
|------:|------|-------|
| **0** | **`ItemWorld` facade** | One type wrapping existing stores behind id-keyed accessors. No storage change, no behaviour change. Later stages mutate *inside* the facade. |
| **1** | **Split the god-component** | `WorkspaceItemState` remains the **project-file DTO** (`projectfile.cpp` serializes it; `projectfile_roundtrip` pins the shape). Runtime gets separate tables (see below). |
| **2** | **Demote `ImageItem` to a render proxy** | Highest payoff. Item keeps what Qt needs to draw (pixmap, surface id, transform derived from Placement). Interaction scratch → `ItemInteractSession` (already exists). Tile-LOD cache → runtime-only table under `DisplayPipelineController`. Collapse `captureState` fan-out into Placement / component writes. |
| **3** | **Systems as free functions** | Entry points: `system(ItemWorld&, std::span<const SessionImageId>)` (or equivalent). GalleryLayout / ContentXform already lean this way; remove `ImageItem*` from pure transforms where possible. |
| **4** | **Persistence split** | Tag each table persistent vs derived. Project save walks only persistent tables. Today the distinction is implicit (`includePose` flags, path-book vs appearance). |
| **5** | **Storage (optional)** | Dense index + contiguous arrays *behind* `ItemWorld`. Only if profiled. |

#### Stage 1 runtime tables (sketch)

```text
dense:   Placement  { pos, scaleX, scaleY, shear, rotation, z, hFlip, vFlip }
         Render     { opacity }
         SourceSize { QSize }              // = ImageSizeBook (already exists)
sparse:  ContentBake { quarterTurns, hFlip, vFlip }
         Crop        { rect, sourceSize, rotation }
         Attention   { points }
         Color       { ColorAdjustments }
```

The three `hasX` bools become **presence in a sparse table**.
`syncAttentionPrimary()` disappears with the duplicated primary/list fields.

### Sequencing

- **Stage 2 is the valuable one; Stage 1 is its prerequisite.**
- Early payoff, lower risk: Stage 0 → Stage 1 for **Crop and Attention only**.
  Those already have controllers, pure geometry (`CropGeometry`,
  `AttentionGeometry`), and characterization coverage — safest components to
  lift out of the fat struct first.
- **Placement last** — every mode touches it.
- **Phase 6 Tier 4 residual is not replaced by Phase 7.** Overlay storage is
  in place; trusting document-only pack still needs the offscreen ImageView
  harness ([docs/IMAGEVIEW_CHARACTERIZATION.md](docs/IMAGEVIEW_CHARACTERIZATION.md)).
  Pack order is a session-list authority problem; Phase 7 is per-id component
  ownership. They can proceed in parallel, but do not conflate the two exit
  criteria.

### Characterization

`tests/session_gallery_crop_scenario_test.cpp` and the dual-model suite are the
right harness spine. Before Stage 1 lands runtime tables, extend pure tests to
assert **component presence** (crop/attention keyed by id) rather than only
fat-struct fields. Offscreen ImageView harness remains the gate for live
decode/framing assertions (Phase 6 Tier 4).

### Rules (additions)

Phase 1–6 rules still apply. Additions:

- No behaviour change without a failing scenario or explicit product decision.
- Do not rename or reshape the on-disk / project `WorkspaceItemState` DTO until
  Stage 4 has a migration story; runtime tables may differ earlier.
- Do not touch `QGraphicsScene` ownership or introduce a frame scheduler.
- Prefer deleting sync paths (`captureState` fan-out, path-book write-through for
  bound ids) over new mirror flags.
- Ship Stage 0 as a pure facade tip before any table split.

### Exit criteria (whole phase)

- One authoritative owner per persisted fact keyed by `SessionImageId` (pose
  may remain Workspace-mode-scoped; content/crop/attention/color are id-keyed).
- `ImageItem` is a render + hit-test proxy, not a parallel appearance database.
- Project save walks explicitly tagged persistent tables.
- `git grep captureState` is thin (host snapshot for DTO only) or gone from
  interaction hot paths.
- Phase 6 Tier 4 residual still tracked separately until the full ImageView
  characterization harness is green (member `m_pathOrderBook` already gone).

### Progress log (Phase 7)

- biltoo-1725: **Stage 0** — `ItemWorld` facade (`itemworld.h`); ImageView binds
  path/size books in ctor and appearance in `bindSessionAppearance`;
  `appearance()` / `hostItemStateBook` route through the facade;
  `tests/itemworld_test.cpp` locks pure pointer identity + id/path/size API.
- biltoo-1726: **Stage 1 (Crop + Attention)** — `itemcomponents.h` extract/apply;
  ItemWorld owns sparse crop/attention tables dual-written with DTO on
  `setAppearance` / `setCrop` / `setAttention`; `setSessionAppearance` routes
  through ItemWorld; tests cover presence, clear, remove, DTO-direct fallback.
- biltoo-1727: route all `appearance().set` / remove / clear through ItemWorld
  (ImageView, Crop/Attention/Workspace/DisplayPipeline controllers, MainWindow);
  `clearAppearance` clears DTO + sparse tables.
- biltoo-1728: Stage 1 residual — ContentBake + Color components and sparse
  tables on ItemWorld (dual-write with DTO; identity ⇒ absent).
- biltoo-1729: Attention read/write via `itemWorld().attention` / `setAttention`;
  colour grade via `setColor` (path/id backfill on DTO when needed).
- biltoo-1730: bake/crop path-book + presence via ItemWorld — `getAppearance` /
  `getPathState` / `setPathState` in bake; crop store/load unbound path via
  ItemWorld; crop apply seeds use `hasCrop()`.
- biltoo-1731: all appearance **reads** via ItemWorld — `getAppearance` /
  `hasAppearance` / `appearanceValue`; controllers and ImageView no longer call
  `appearance().get/value/contains` for DTO lookup.
- biltoo-1732: **Stage 2 start** — Placement component + sparse table (always
  dual-written on setAppearance); captureState path-book reads via getPathState.
- biltoo-1733: path-book accessors via ItemWorld; layout pose via setPlacement;
  setItemStateForPath / gallery / display / slideshow path reads through facade.
- biltoo-1734: thin captureState / captureContentBakeBeforeState /
  appearanceCropMapForEdit — bound content orient and crop meta from sparse
  ContentBake + Crop components (not fat DTO field peeks).
- biltoo-1735: `applyPlacement` + `applyState` via Placement component;
  `rememberItemState` writes setPlacement before setAppearance.
- biltoo-1736: session-gallery-crop-scenario asserts ItemWorld Crop/ContentBake
  presence; IMAGEVIEW_CHARACTERIZATION pure-contract table updated.
- biltoo-1737: `placementFromItem` + captureState pose via applyPlacementToState
  (single reader for live ImageItem pose).
- biltoo-1738: `applyGeometrySessionState` — geometry undo/redo syncs ItemWorld
  Placement + appearance (or path book); both TransformCommand sites use it.
- biltoo-1739: `persistGeometrySessionState` on forward path of
  pushItemGeometryCommand / pushItemTransformUndo (item already posed; tables catch up).
- biltoo-1740: `pushItemTransformUndo` delegates to `pushItemGeometryCommand`
  (one TransformCommand implementation).
- biltoo-1741: `placementNearlyEqual` — move/rotate no-op check is Placement-shaped.
- biltoo-1742: ItemInteractSession stores `dragStartPlacement` (Stage 2 interact
  scratch); beginMove/Rotate/HandleDrag fill it from the start DTO.
- biltoo-1743: GroupTransformSession stores parallel `dragStartPlacements`;
  prune/align keep placement list in sync with items/states.
- biltoo-1744: group scale/rotate mid-drag reads `dragStartPlacementAt` (not
  full DTO fields).
- biltoo-1745: free-rotate mid-drag uses `currentDragStartPlacement().rotation`;
  ImageItem `m_press*` handle anchors remain Stage 2 residual (tile LOD next).
- biltoo-1746: ImageItem handle press fields → `HandlePressScratch m_handlePress`
  (named Stage 2 residual; clear on endHandleInteraction).
- biltoo-1747: HandlePressScratch type on ItemInteractSession; dual-store —
  session copies press at beginHandleDrag; ImageItem still owns mid-drag math.
- biltoo-1748: handle press owned only by ItemInteractSession; updateHandle /
  applyScale/Shear take HandlePressScratch&; ImageItem m_handlePress removed.
- biltoo-1749: ItemInteractSession::endRotate() alias (parity with endMove /
  endHandleDrag); fixes free-rotate mouse release build break.
- biltoo-1750: HandlePressScratch::handle owns continuous handle for mid-drag;
  update/applyScale/applyShear read press.handle; m_activeHandle paint residual.
- biltoo-1751: continuous-drag authority on HandlePressScratch (hasContinuousHandle);
  endHandleInteraction(Handle) from press; hasActiveHandle paint residual only.
- biltoo-1752: HandlePressScratch pose is Placement; ImageItem::placement() single
  live reader; placementFromItem delegates; mid-drag uses press.placement.
- biltoo-1753: ItemHandle enum extracted (itemhandle.h); HandlePressScratch typed;
  ItemHandlePolicy independent of ImageItem.
- biltoo-1754: drop dead rotateItemStart; beginRotate three-arg; remove hasActiveHandle
  (press authority); m_activeHandle paint residual only.
- biltoo-1755: remove m_activeHandle; continuous begin sets hover (frozen while
  isHandleDragging); chrome paint hot is hover-only.
- biltoo-1756: drop dead m_orientation/m_fineRotation and orient APIs; placement
  rotation is sole free-rotate field.
- biltoo-1757: ImageItem::applyPlacement single live pose writer; ImageView
  applyPlacement thin-forwards.
- biltoo-1758: GroupTransformSession paint hot via hover (beginDrag sticks
  hoverHandle); isHandleHot hover-only; handle stays mid-drag authority.
- biltoo-1759: GroupTransformSession::currentHandle(); drop unused hasActiveHandle;
  updateGroupScale reads accessor.
- biltoo-1760: GroupTransformSession dragStartStateAt/hasDragItems; release undo and
  canvas-focus use accessors.
- biltoo-1761: GroupTransformSession data members private (accessor-only API).
- biltoo-1762: ItemInteractSession class with private members (parity with group session).
- biltoo-1763: handle-drag scale/shear/rotate mid-drag writes via applyPlacement.
- biltoo-1764: group scale/rotate mid-drag writes via applyPlacement.
- biltoo-1765: workspace free-rotate mid-drag writes via applyPlacement.
- biltoo-1766: duplicateSelected pose via applyPlacement (Workspace copy + Gallery identity).
- biltoo-1767: drop/bind identity and Image-mode reset pose via applyPlacement.
- biltoo-1768: GalleryLayout pack pose via applyPackPose / applyPlacement.
- biltoo-1769: framing scale (and Image-mode pos) reset via applyPlacement.
- biltoo-1770: Gallery enter / pre-pack free-form clear via applyPlacement.
- biltoo-1771: CropSession draft/enter/restore/commit pose via applyPlacement.
- biltoo-1772: transform commands, workspace shear/layout, slideshow, footprint via applyPlacement.
- biltoo-1773: chrome reset handles, opacity slider, placement flips via applyPlacement.
- biltoo-1774: fix duplicate GroupTransformSession::dragStartStateAt declaration.
- biltoo-1775: ImageItem::zoomBy/rotateBy via applyPlacement.
- biltoo-1776: placementFromState/applyPlacementToState pure characterization tests.
- biltoo-1777: privatize setItem*/setStackZ; residual setPos/setStackZ via applyPlacement.
- biltoo-1778: itemworld test links SessionAppearanceStore (sessionappearance.cpp + deps).
- biltoo-1779: bindSelectedSessionIds / crop stash / footprint scale via Placement.
- biltoo-1780: imageitem_tilelod.cpp — tile LOD methods out of interaction TU.
- biltoo-1781: tilelod::ItemBag — controller + scratch on ImageItem (demotion prep).
- biltoo-1782: dropItemTileLodSession pipeline API; controllers no longer drop on item.
- biltoo-1783: tickItemTileLod / setItemTileLodSuppressed; coordinator via pipeline.
- biltoo-1784: privatize ImageItem tile mutators; friend pipeline + CropSession.
- biltoo-1785: destroyCanvasItem drops tile session via pipeline before delete.
- biltoo-1786: privatize tile plan/paint helpers on ImageItem.
- biltoo-1787: imageview characterization pure scaffold (PNG fixtures + QSKIP for ImageView).
- biltoo-1788: BILTOO_LIB_SOURCES shared list (app + future ImageView harness).
- biltoo-1881: PackOrderOverlay design type + pure tests.
- biltoo-1882: optional nix ccache via `.#biltoo.withCcache`.
- biltoo-1883: ImageView stores PackOrderOverlay (host mutators Explicit).
- biltoo-1884: tryCollapseToFollowDocument on aligned setOrder; seed-on-append.
- biltoo-1885: imageview-characterization pure overlay host simulation.
- biltoo-1886: pathorder-dual-model overlay dual-model cases.
- biltoo-1887: remove dead PackOrderReadSource / packOrderForRead.
- biltoo-1888: REFACTOR Tier 4 residual docs match overlay storage.
- biltoo-1889: REFACTOR historical path-order wording cleanup.
- biltoo-1890: imageview.h orphan comments; drop PagePath::kMarker; CMake option note.
- biltoo-1891: biltoo_lib STATIC; CHARACTERIZATION=ON links biltoo_lib.
- biltoo-1892: ImageView characterization body (enterGallery, pack, crop, LoadAdd).
- biltoo-1893: REFACTOR progress log through 1892.
- biltoo-1894: pathOrderOccurrences inline on host pipeline.
- biltoo-1895: delete imageview_load.cpp; pathOnLiveCanvas → canvas TU.
- biltoo-1896: merge imageview_pack mode-dispatch into imageview_modes.
- biltoo-1897: fold imageview_gallery + edge_chrome into modes/paint/input.
- biltoo-1898: merge transform undo helpers into transform_actions.
- biltoo-1899: merge shell_events + dnd into input_events.
- biltoo-1900: merge background settings into paint_background.
- biltoo-1901: merge imageview_framing into framing_image.
- biltoo-1902: merge canvas_focus into canvas TU.
- biltoo-1903: merge color_grade into appearance TU.
- biltoo-1904: merge crop appearance helpers into appearance TU.
- biltoo-1905: merge clipboard into selection TU.
- biltoo-1906: split imageview_view grab-bag into modes/accessors/size_book.
- biltoo-1907: merge workspace scene/placement into canvas TU.
- biltoo-1908: merge slideshow + HUD overlay paint into paint TU.
- biltoo-1909: REFACTOR metrics after ImageView TU fold (1894–1908).
- biltoo-1910: privatize paint/input try* phases off host_pipeline public surface.
- biltoo-1911: privatize pathOrderAppendRow + geometry session state helpers; TransformGeometryCommand friend.
- biltoo-1912: imageview.h orphan comments after host/Slideshow moves; ~616 lines.
- biltoo-1913: findItemBySessionId + matchesLoadGeneration on host surface; ~609 lines.
- biltoo-1914: host_ops vs host_accessors — gallery/page-guide ops off accessors file.
- biltoo-1915: host bags (appearance/session/HUD/loadGate/…) in accessors; crop_display ops-only; ~598 lines.
- biltoo-1916: private_methods blank trim; host include banners; REFACTOR metrics/friend/load TU notes.
- biltoo-1917: pure characterization Placement survives pathOrderClear; PackOrderOverlay docs.
- biltoo-1918: pure characterization ContentBake + Color survive pathOrderClear.
- biltoo-1919: pure characterization Attention survives pathOrderClear (component family complete).
- biltoo-1920: full ImageView harness asserts all id-keyed components survive pathOrderClear.
- biltoo-1921: pure characterization verified green (16 pass / 1 skip) on Qt 6.11 nix develop.
- biltoo-1922: Gallery path lookup prefers findPreferredItemForPath (duplicate identity).
- biltoo-1923: addImage / focusSessionPath / classic paint prefer preferred-item path lookup.
- biltoo-1924: focusGalleryItem; path focus delegates; Gallery keyboard uses item pointer.
- biltoo-1925: focusSessionId; MainWindow session cursor prefers id over path.
- biltoo-1926: Gallery viewport restore stores/prefers focus SessionImageId.
- biltoo-1927: Gallery layout-switch multi-select restore by SessionImageId.
- biltoo-1928: findItemForPath consolidates preferred-then-first path lookup.
- biltoo-1929: persistGeometrySessionState bound-id path is Placement-only (no setAppearance).
- biltoo-1930: geometry undo command + APIs store/apply Placement only.
- biltoo-1931: interact/group drag-start is Placement only (no fat DTO anchor).
- biltoo-1932: project save workspace poses are Placement (no captureState).
- biltoo-1933: free-form Gallery pose snapshot is Placement by session id.
- biltoo-1934: Gallery pack after-callback is Placement-only (no captureState).
- biltoo-1935: captureState prefers ItemWorld sparse tables for bound ids.
- biltoo-1936: rememberItemState setAppearance-only; crop undo uses captureState alone.
- biltoo-1937: content-bake before/after state does not overwrite sparse tables from item.
- biltoo-1938: syncLiveContentMetaFromState / clearLiveContentMeta — single dual-write install for live crop/flip; bound content-mods prefer ItemWorld.
- biltoo-1939: residual crop/flip readers prefer ItemWorld; drop seedEnterCropFlags; selection/bind no longer overwrite captureState from item.
- biltoo-1940: crop draft enter clears live meta via clearLiveContentMeta; drop applyKeep/EnterDraftFlags + fillAppearanceFromItemSessionCrop; framing draft uses live session crop only.
- biltoo-1941: wantAppearanceForItem lag-fill prefers ItemWorld ContentBake/Crop; live item fields fallback only.
- biltoo-1942: unbound remember/persist path-book via captureState only; sessionCropApplied prefers ItemWorld hasCrop.
- biltoo-1943: bakeItemRotate90 want flips/grade from beforeSt only (no live item dig).
- biltoo-1944: chrome crop/orient/flip marks via tileContentXform only (single live content-meta reader).
- biltoo-1945: captureState/framing/wantAppearance/CropSession live digs via tileContentXform; corner marks completed.
- biltoo-1946: no-view flip/rotate fallback via tileContentXform + setters; session fields only dual-write install + tileContentXform.
- biltoo-1947: syncLiveContentMetaFromState always sets applied ContentXform (primary tileContentXform path).
- biltoo-1948: identity clear (reset / path-change) drops applied with session dual-write fields.
- biltoo-1949: drop unused session crop/flip getters; dual-write setters install-only; reads via tileContentXform.
- biltoo-1950: tileContentXform public; dual-write setters private (friend ImageView).
- biltoo-1951: crop draft enter via syncLiveContentMetaFromState(contentOnly); document pixel-clear dual-write lag.
- biltoo-1952: bake rotate/flip drop redundant setApplied; applied only via syncLive/attachDisplaySample/no-view.
- biltoo-1953: set/clearAppliedContentXform private (friend ImageView); public getters retained.
- biltoo-1954: syncLive installs applied only; clearDecodedPixels seeds lag dual-write from applied.
- biltoo-1955: clearLiveContentMeta always identity (lag+applied); no-view flip applied-only.

- biltoo-1789: QFileInfo include in imageitem_tilelod.cpp (TU split fix).
- biltoo-1790: ImageItem/pipeline tileLodBag() single access path (ownership prep).
- biltoo-1791: ItemBag lazy unique_ptr on ImageItem (move-ready).
- biltoo-1792: DisplayPipelineController owns ItemBag map; item attach/detach.
- biltoo-1793: tileLodBag() ensures pipeline bag via ImageView when on scene.
- biltoo-1794: registerItemDisplaySurface ensures pipeline tile bag.
- biltoo-1795: m_tileBags as unordered_map (unique_ptr-safe).
- biltoo-1796: local bag debug warning; dropAll resets m_tileBags entries.
- biltoo-1797: releaseAllTileBags before scene clear / pipeline dtor.
- biltoo-1798: gallery/workspace discardStash releases pipeline tile bags.
- biltoo-1799: gallery restoreStashedItems releases bags for residual live items.
- biltoo-1800: gallery restore residual via destroyCanvasItem; discardStash unregisters display surface.
- biltoo-1801: crop tile LOD suppress via pipeline; CropSession no longer friends ImageItem tile mutators.
- biltoo-1802: drop ImageItem local m_tileLod; pipeline map is sole ItemBag owner.
- biltoo-1803: dropItemTileLodSession / dropTileLodSession no longer ensure a bag.
- biltoo-1804: pipeline owns tile suppress write; ImageItem setTileLodSuppressed removed.
- biltoo-1805: contentxform sourceToDisplayTransform_matchesMapCorners fixture fix.
- biltoo-1806: tileLodBag no ImageView ensure (attached bag only).
- biltoo-1807: drop dead ImageItem dropTileLodSession / invalidateTilePathRam.
- biltoo-1808: pipeline tickPrimaryTileLod without m_view hop.
- biltoo-1809: Gallery/Slideshow/ImageView ticks via hostDisplayPipeline / m_displayPipeline.
- biltoo-1810: drop ImageView tile thin-forwards (tick/purge/dropAll/schedule).
- biltoo-1811: drop ImageView::applyPlacement thin-forward; applyState writes item Placement.
  **Stage 2 tile LOD ownership treated complete** (bag, suppress, tick, drop, purge on
  DisplayPipelineController; ImageItem keeps paint/plan helpers + query predicates).
- biltoo-1812: drop dead ImageItem zoomBy/rotateBy/toggleHFlip/toggleVFlip.
- biltoo-1813: drop dead ImageItem itemScale/itemShear/itemOpacity/updateHandleLayout.
- biltoo-1814: drop dead ImageItem::cropToLocalRect + scaleHandlesEnabled getter.
- biltoo-1815: drop dead chrome helpers (deviceScaleMin, handleHitRadius, drawCornerBracket, chromeButtonSize, handleDistanceScreenPx).
- biltoo-1816: drop dead handleDrawSize + activeHandles.
- biltoo-1817: drop unused pathOrder/pathOrderPaths/pathOrderIds/sessionIdOrder; reads via currentPackOrder only.
- biltoo-1818: drop unused pathOrderSize/pathOrderPathAt/pathOrderIdAt.
- biltoo-1819: ImageItem handle classifiers → ItemHandlePolicy direct (drop thin wrappers).
- biltoo-1820: drop ImageItem::handleToolTip + dead ImageView::seedEmptyWorkspaceFromReplace forward.
- biltoo-1821: drop dead ImageView::imageModeItemForPath thin-forward.
- biltoo-1822: gallerySoftResetPath/All via pipeline host; drop ImageView thin-forwards.
- biltoo-1823: scheduleGalleryDecode via pipeline; drop ImageView thin-forward.
- biltoo-1824: ensureWorkspaceQualityClimb + galleryDisplayEdgeForItem via pipeline;
  drop dead native-decode/pending-tile/edge ImageView forwards.
- biltoo-1825: ladderReady lambda → pipeline onLadderReady; drop ladder ImageView forwards.
- biltoo-1826: scheduleImageLoad + onImageLoaded/Preview via pipeline; drop load/replace ImageView forwards.
- biltoo-1827: display surface register/unregister + focus drive + image-mode climb via pipeline.
- biltoo-1828: installDisplayPixels via pipeline host; drop ImageView install + preserving-view forwards.
- biltoo-1829: createPlaceholder + appearance seed via pipeline; drop dead createItem/bind/seed ImageView forwards.
- biltoo-1830: fix pipeline host lambdas (onImagePreviewLoaded / seed APIs via hostDisplayPipeline).
- biltoo-1831: contentxform matchesMapCorners — no QVERIFY-in-lambda; crop from oriented AABB.
- biltoo-1832: flake biltoo-test helper (build + ctest offscreen).
