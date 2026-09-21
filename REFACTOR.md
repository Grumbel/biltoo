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

**Safe next steps:** characterization defaults to full harness (`biltoo_lib`);
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
   read-source API (1887). `biltoo_lib` + characterization full harness (1891;
   default ON since 1996). Offscreen ImageView body exercises pack/crop/LoadAdd
   without decode wait (1892). **Full harness green** (1973: 17 pass / 0 skip).
   **Still open:** optional decode/framing assertions. Low-RAM: configure with
   `-DBILTOO_IMAGEVIEW_CHARACTERIZATION=OFF`. See PATH_ORDER.md /
   IMAGEVIEW_CHARACTERIZATION.md.
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
| **Component stores** | `ItemWorld` sparse tables (Crop / Attention / ContentBake / Color / Placement), `SessionSeedBook` (seed attempts), `PathItemStateBook`, `ImageSizeBook`, `GallerySoftBook`, `PendingItemAppearanceBook`, DisplaySurface bindings |
| **Systems** | `ContentXform`, `CropGeometry`, `GalleryLayout`, `AttentionGeometry`, `PlacementLinear`, `ItemFrameGeometry`, `HudModel` — largely stateless functions over data |

### The two structural problems left

1. **`WorkspaceItemState` is a fat god-component.** One struct holds identity,
   Workspace pose, content bake flags, crop, attention, and color. Optional
   sub-records use bool flags (`hasCrop`, `hasAttention`) plus duplicated
   flip fields (`hFlip` / `contentHFlip`). Optional-by-bool inside a fat
   struct is exactly what component **presence** replaces. `syncAttentionPrimary()`
   exists only to keep primary and list copies aligned inside the same struct.

2. **The same per-item facts live in fewer places (Stage 4b reduced this).**
   - `ItemWorld` sparse tables keyed by id (durable content + Workspace placement)
   - `PathItemStateBook` keyed by path (Workspace unbound / placement cache)
   - `ImageItem` members still hold applied ContentXform, live pose, color paint
     copy, press-anchor scratch, tile-LOD cache — Stage 2 residual demotion

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
| **3** | **Systems as free functions** | Entry points: `system(ItemWorld&, std::span<const SessionImageId>)` (or equivalent). GalleryLayout pack pure data plane complete (2030–2039): all `packPoses*` + DRY apply; ContentXform already pure. Further systems as needed. |
| **4** | **Persistence split** | Tag each table persistent vs derived. Project save walks only persistent tables. Design: see **Stage 4 design** below (tip 2020). |
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

#### Stage 4 design (tip 2020)

**Goal.** Project save/load and clipboard walk **explicitly tagged persistent
tables**, not a fat DTO that is also the runtime dual-write mirror. Dual-write
(sparse tables → fat `WorkspaceItemState`) remains until Stage 4 ships a
versioned project format that no longer needs the mirror for round-trip.

**Today (implicit persistence)**

| Fact | Runtime | On disk (project JSON) |
|------|---------|------------------------|
| Path + `SessionImageId` | `SessionDocument` | `images[]` row |
| Crop / orient / grade / attention | ItemWorld sparse + fat DTO | `appearanceToJson` fields (always when present) |
| Workspace pose | ItemWorld Placement / fat DTO | Same object, gated by `includePose` / `hasWorkspacePose` |
| Path book (unbound) | `PathItemStateBook` | Not a first-class project key; unbound is rare in saved projects |
| Applied ContentXform, tiles, soft pixels | Live only | **Never** persisted |
| List order | Document index | Array order of `images[]` |
| `sessionIndex` on DTO | Deprecated cache | Should not be required for load |

`ProjectImage` already splits **content appearance** vs **workspace pose** via
`hasWorkspacePose` + `mergePoseIntoProjectImage`. That is the seed of Stage 4.

**Persistent vs derived (target tagging)**

| Table / fact | Tag | Notes |
|--------------|-----|-------|
| SessionDocument paths + ids | **Persistent** | Source of truth for list identity |
| Crop, ContentBake, Color, Attention | **Persistent** | Sparse ItemWorld tables; project fields map 1:1 |
| Placement (Workspace pose) | **Persistent** (Workspace-scoped) | Optional per row; Gallery/Image may omit |
| Path book | **Persistent** only for unbound | Bound crop must not appear (IDENTITY) |
| Fat `WorkspaceItemState` | **Derived mirror** | Dual-write for legacy serializers until format bump |
| Applied ContentXform, ImageCache, tile LOD | **Derived** | Rebuild from host + persistent components |
| `sessionIndex` on items/DTO | **Derived** | `sessionListIndex` from document |

**Format migration (do not break existing projects)**

1. **Keep** `WorkspaceItemState` / `appearanceToJson` / `appearanceFromJson` as
   the **on-disk DTO** until a new `projectFormat` version is introduced.
2. **Save path (Stage 4a):** build the DTO **only at the project boundary** from
   sparse tables + document (`appearanceValue` / component getters), not by
   reading a dual-written fat store as authority. Runtime can keep dual-write
   for a while; save must not depend on it being complete.
3. **Load path (Stage 4a):** `appearanceFromJson` → `setAppearance` (or
   component setters) so sparse tables are populated; do not leave fat-only.
4. **Stage 4b (optional format bump):** serialize sparse objects explicitly
   (`"crop": {...}`, `"placement": {...}`) instead of flat DTO keys; keep a
   reader for the old flat shape. Only then can dual-write be removed.
5. **Clipboard** uses the same DTO helpers as project save (`includePose=true`);
   treat clipboard as the same persistence boundary.

**Non-goals**

- No Stage 5 dense storage as part of Stage 4.
- No behaviour change to IDENTITY path-map crop rules.
- No renaming on-disk keys without a format version + reader.
- No persisting applied ContentXform or decode caches.

**Exit criteria (Stage 4)**

- [x] Documented tag table (above) matches code comments on ItemWorld stores.
- [x] Project save builds appearance from **sparse-prefer reads**
  (`sessionAppearanceValue` / `appearanceValue`), never from a raw fat pointer alone.
- [x] Load always dual-fills sparse tables (`setAppearance` / component sync).
- [x] `git grep setAppearance` on hot edit paths may still dual-write; save/load
  no longer *require* dual-write correctness for any single field.
- [x] Characterization: `projectfile_roundtrip` still green; pose-only vs
  content-only rows covered (biltoo-2027).

**Stage 4a status: complete** (tips 2020–2027 + store-read hygiene 2022–2042).

**Session appearance lifecycle (Stage 2 residual → Stage 4b residual)**

| Operation | Seed book (`SessionSeedBook`) | Sparse content (`ItemWorld`) |
|-----------|-------------------------------|------------------------------|
| Open / Replace (`setPaths` + `applyExpandedLoad`) | cleared in `setPaths` | `clearAppearance` in `applyExpandedLoad` |
| newSession / project wipe (`clear` + `clearWorkspace`) | `SessionDocument::clear` | `clearWorkspace` → `clearAppearance` |
| Sort / reorder (`replaceAll`) | **kept** (same ids) | **kept** |
| Remove row (`removeAt` + view) | `removeAt` drops seed flag | `removeAppearance` (Image) or `removeWorkspaceSessionId` (W/G) |
| `clearPaths` only | **kept** (intentional; paths empty) | caller must clear if full wipe |

Ids are never recycled (`IDENTITY`). Content is sparse-only (no fat DTO mirror).
`SessionSeedBook` tracks XDG seed attempts only. Contract tests:
`sessiondocument_test` (seeds), `itemworld_test` (sparse content).


**Stage 4b status: complete** (tips 2051–2064; path-book IDENTITY 2069–2071)

Stage 4b delivered (product: no backward compatibility):

1. Project + clipboard `version` ≥ 2; load rejects `< 2`
2. Nested on-disk shape: `crop` / `attention` / `bake` / `color` / `placement` objects
3. Component mutators + `setAppearance` sparse-only; `appearanceValue` assembles from sparse
4. `SessionSeedBook` is seed-attempt only (no fat `WorkspaceItemState` map)
5. Write-path hygiene (2060–2063): `mergeContentFromState` (XDG seed), crop/bake
   component writes, `clearContentComponents` (Reset Content), `captureState` without
   live gap-fill; `setAppearance` preserves existing Placement on identity pose
6. Path-book IDENTITY (2069–2071): bound `setPathState` strips all content; bake
   path sync unbound-only; bound readers use sparse + XDG only

**Runtime write APIs (single source of truth)**

| Intent | API |
|--------|-----|
| Full replace (load / bind / snapshot) | `setAppearance` |
| Content upsert (XDG seed) | `mergeContentFromState` |
| Crop commit / orient bake | `setCrop` + `setContentBake` |
| Content reset | `clearContentComponents` |
| Pose only | `setPlacement` |
| Attention / color | `setAttention` / `setColor` |

**Stage 4 residual: closed** (tips 2051–2064). Further tips only for regressions.

### Stage 2 residual — `ImageItem` still holds (intentional render proxy)

| Member / concern | Role | Demotion notes |
|------------------|------|----------------|
| Pixmap / preview / intrinsic size | Qt paint | Keep |
| `m_colorAdjust` | Live grade for paint + slider lag | Paint uses liveColorForPaint → host lag when bound (2091/2093); durable is Color |
| Applied `ContentXform` fingerprint | Mid-edit content authority | ItemWorld when bound (2080–2084); paint prefers host helpers (2093); item mirror dual-written |
| Live pose (`m_scaleX`… via `applyPlacement`) | QGraphicsItem transform | Keep; durable copy is ItemWorld Placement |
| `sessionIndex` cache | List-order mirror | Prefer `sessionListIndex` / document; `refreshSessionIndexCache` clears unbound (2089); pack hint restamped by caller |
| Tile LOD bag pointer | Runtime decode | Already pipeline-owned bag |
| `PendingItemAppearanceBook` | Duplicate→bind staging | GUI-only; destination is `setAppearance` on bind |
| `PathItemStateBook` | Unbound content + placement | Bound content stripped on write (2069); bound reads sparse+XDG (2070–2071) |


**Sequencing relative to Stages 0–4**

Stages 0–4b are complete (facade, sparse tables, nested format, path-book IDENTITY).
Stage 3 remains incremental. Stage 2 residual for host authority paths and paint
preference is largely complete (2080–2093): applied ContentXform, sessionIndex,
liveColorLag, itemLiveColor / itemAppliedContentXform, live*ForPaint. ImageItem
still holds intentional render-proxy state (pixmap, dual-written mirrors for
detached tiles, live pose, host-raw pixmap grade in updateDisplayedPixmap).
Phase 6 Tier 4 decode/framing is independent.

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
- On-disk project appearance is nested v2 (Stage 4b). Runtime DTO remains the
  assemble/load boundary; further DTO field drops need explicit product decision.
- Do not touch `QGraphicsScene` ownership or introduce a frame scheduler.
- Prefer deleting sync paths (`captureState` fan-out) over new mirror flags.
  Path-book write-through for bound content is gone (2069–2071).
- Ship Stage 0 as a pure facade tip before any table split.

### Exit criteria (whole phase)

- [x] One authoritative owner per persisted fact keyed by `SessionImageId` (pose
  may remain Workspace-mode-scoped; content/crop/attention/color are id-keyed).
- [x] `ImageItem` is a render + hit-test proxy, not a parallel appearance database.
- [x] Project save walks explicitly tagged persistent tables (Stage 4a sparse-prefer).
- [x] `git grep captureState` is thin (2042): definition, `freezeItemAppearance`
  fallback, and crop-enter undo baseline only — not interaction hot paths.
- [x] Stage 4b: format version ≥ 2 nested sparse + drop dual-write (no v1 reader).
- [x] Path-book content for bound `SessionImageId` is write- and read-clean (2069–2071).
- Phase 6 Tier 4 non-async residual complete through biltoo-2101
  ([docs/IMAGEVIEW_CHARACTERIZATION.md](docs/IMAGEVIEW_CHARACTERIZATION.md));
  optional: full async PreferCache / thumtoo ladder.

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
- biltoo-1956: seedContentMetaLagFromApplied / clearContentMetaLag; drop private dual-write setters.
- biltoo-1957: syncLiveColorFromState centralizes live grade install; liveItemHasContentMods takes ContentXform.
- biltoo-1958: setColorAdjustments/Record private (friend ImageView); writers only via syncLiveColorFromState.
- biltoo-1959: selection/session-bind drop post-captureState live digs (color/applied).
- biltoo-1960: captureState prefers applied ContentXform over sparse tables; drop commit force digs.
- biltoo-1961: wantAppearanceForItem prefers applied/tileContentXform; lag-fill only without applied.
- biltoo-1962: mergeLiveContentLagFlags (no applied path); framing single tileContentXform read.
- biltoo-1963: content-meta Stage 2 status docs + dual-write→lag/applied comment cleanup.
- **Content-meta Stage 2 demotion (1938–1966):** ImageItem is no longer a parallel
  crop/flip store. Authority: ItemWorld sparse tables (bound) + applied ContentXform
  mid-edit; live read via `tileContentXform` (applied-only); install via
  `syncLiveContentMetaFromState`. Lag fields removed in 1966. Color parallel live
  grade remains (`syncLiveColorFromState` / private mutators).
- biltoo-1964: captureState/wantAppearance prefer live color grade (interaction authority).
- biltoo-1965: clearDecodedPixels keeps applied ContentXform; drop seedContentMetaLagFromApplied.
- biltoo-1966: drop content-meta lag fields; tileContentXform is applied-only.
- biltoo-1967: rename mergeLiveContentLagFlags → fillEmptyContentFlags; wantAppearance
  sparse fill without dead tileContentXform call; crop-locked color flush re-schedules.
- biltoo-1968: privatize bakeRotate90/bakeFlip; appliedContentXform → tileContentXform
  alias; call sites prefer tileContentXform; setTargetColor docs live vs ItemWorld.
- biltoo-1969: drop public itemHFlip/VFlip and appliedContentXform alias; placement()
  is sole pose-flip reader; tileContentXform sole content-meta value reader.
- biltoo-1970: drop public itemScaleX/Y/itemRotation; placement() is sole live pose
  reader (stackZ remains for z-order sorts).
- biltoo-1971: drop public stackZ(); placement().z is sole z-order reader.
- biltoo-1972: private setSource/setPreview/clearDecodedPixels; host APIs
  clearItemDecodedPixels / setItemPreviewImage for non-friend controllers.
- biltoo-1973: CHARACTERIZATION=ON full harness green (17 pass incl. openGalleryCropReturn);
  wantAppearance prefers ItemWorld::color(id) for stored grade.
- biltoo-1974: sparse Color prefer on durable persist + flushColorAdjustCommit store base.
- biltoo-1975: targetHasContentAppearance includes hasColor; createItem prefers sparse grade.
- biltoo-1976: filmstrip imageWithSessionAppearance + attachDisplaySample + load prefer sparse Color.
- biltoo-1977: Reset / clearedContentOps clear colour grade (align help + hasContentAppearance).
- biltoo-1978: sessionListIndex from SessionDocument; find/select by index prefer id.
- biltoo-1979: MainWindow/controllers/open paths use sessionListIndex for list order.
- biltoo-1980: sessionAppearanceValue sparse-prefer choke; captureState uses sessionListIndex.
- biltoo-1981: flush/setTarget/peer/copy/reset/crop use sessionAppearanceValue store reads.
- biltoo-1982: workspace/slideshow/rematerialize/selection/load bind via sessionAppearanceValue.
- biltoo-1983: pipeline want/seed, load restore, canvas rebind, bake placement via sessionAppearanceValue.
- biltoo-1984: resolveStoredAppearance / filmstrip / setColor re-read via sessionAppearanceValue.
- biltoo-1985: privatize setIntrinsicSize + setDisplaySurfaceId; host setItemIntrinsicSize.
- biltoo-1986: privatize setPath + setSessionId/Index; host + GalleryController friend.
- biltoo-1987: privatize setInteractive/GallerySelectable/ScaleHandles/Hover/invalidateDeviceCache.
- biltoo-1988: privatize setGalleryCellSize; GalleryLayout::setItemGalleryCellSize friend helper.
- biltoo-1989: privatize applyPlacement; GalleryLayout::applyItemPlacement; fix cell-size recursion.
- biltoo-1993: SlideshowController + WorkspaceController use GalleryLayout::applyItemPlacement (no new friends).
- biltoo-1994: Q_INIT_RESOURCE(icons) — static biltoo_lib RCC must be forced into the exe.
- biltoo-1995: privatize syncGalleryScrollCache; tests value-read via appearanceValue.
- biltoo-1996: BILTOO_IMAGEVIEW_CHARACTERIZATION defaults ON (full harness).
- biltoo-1997: ImageItem demotion status through 1996; header comment hygiene.
- biltoo-1998: durable sessionIndex from sessionListIndex; no item-cache restamp after captureState.
- biltoo-1999: restore/bind stamp ImageItem sessionIndex cache from sessionListIndex.
- biltoo-2000: rebindWorkspaceSession id→index hash; path-mismatch clears list cache.
- biltoo-2001: pending-bind stamps sessionIndex via sessionListIndex after id bind.
- biltoo-2002: findItemBySessionIndex document-first; no stale-cache cross-tile match.
- biltoo-2003: place/drop pose via persistGeometrySessionState (setPlacement), not setAppearance.
- biltoo-2004: commitItemSessionEdit skips rememberItemState when session id bound.
- biltoo-2005: destroyCanvasItem(persistState); session-id delete does not re-seed appearance.
- biltoo-2006: captureState path-map sessionIndex only for unbound items.
- biltoo-2007: ItemWorld dtoForWrite stamps sessionId on every sparse dual-write.
- biltoo-2008: appearanceValue/setAppearance always stamp sessionId; sparse write test.
- biltoo-2009: workspace snapshot path map only for unbound (no bound crop leak).
- biltoo-2010: restore/bake path map no crop for bound session ids.
- biltoo-2011: ItemWorld setPathState strips crop when state.sessionId is bound.
- biltoo-2012: restore/soft-paint bound never adopt path-map crop.
- biltoo-2013: slideshow content snapshot bound never path/XDG crop.
- biltoo-2014: IDENTITY.md path-map crop ownership (write strip + read bans).
- biltoo-2015: IDENTITY.md sessionId vs sessionIndex; wantAppearance brace cleanup.
- biltoo-2016: IDENTITY.md §2 ItemWorld current; legacy m_itemStates maps historical.
- biltoo-2017: IDENTITY.md §§3–5 mode/duplicate/edit pipeline on SessionImageId.
- biltoo-2018: IDENTITY.md §§7–10 scenarios/invariants id-keyed.
- biltoo-2019: IDENTITY.md §11–12 acceptance and handoff (id + path-map).
- biltoo-2020: Stage 4 design — persistent vs derived tags, 4a/4b migration.
- biltoo-2021: Stage 4a — project/clipboard boundary via sessionAppearanceValue;
  ItemWorld persistence tags; load dual-fills via setSessionAppearance.
- biltoo-2022: Stage 2 residual — captureState seeds from sessionAppearanceValue;
  loadRestoreCropAppearance durable crop via store read.
- biltoo-2023: Stage 2 residual — bake rotate/flip afterSt + store write from want
  + live pose; recordSessionCrop bound path via sessionAppearanceValue.
- biltoo-2024: Stage 2 residual — color flush/setTarget and crop undo afterSt prefer
  sessionAppearanceValue for bound ids (sparse-only safe).
- biltoo-2025: Stage 2 residual — ItemWorld::hasDurableAppearance (fat|sparse);
  store-read gates no longer require fat DTO presence alone.
- biltoo-2026: Stage 2 residual — itemworld_test hasDurableAppearance + sparse-wins
  crop; demotion status through 2025.
- biltoo-2027: Stage 4a characterization — projectfile pose-only vs content-only
  rows (appearanceToJson includePose + mixed project save/load).
- biltoo-2028: Stage 2 residual — remember/persist/workspace snapshot use store +
  live overlays when not mid-edit (applied ContentXform still captureState).
- biltoo-2029: Stage 2 residual — ImageView::freezeItemAppearance consolidates
  store+live freeze policy; remember/persist/snapshot/bind/duplicate/clipboard.
- biltoo-2030: Stage 3 — GalleryLayout::layoutSizeForNative pure helper + test;
  copySessionAppearance donor uses freezeItemAppearance.
- biltoo-2031: Stage 3 — GalleryPackFit characterization (fittedTargets,
  overshootUniformScale, modeFromLayoutMode) in gallerylayout_test.
- biltoo-2032: Stage 3 — scaledDisplaySize / centeredTileBounds pure helpers;
  pack overshoot uses them; column/axis pure helpers tested.
- biltoo-2033: Stage 3 — packPosesSideBySide / packPosesVertical pure data plane;
  pack applies via ImageItem friends only.
- biltoo-2034: Stage 3 — packPosesGrid / packPosesGridCrop pure data plane;
  PackPose.cellSize for GridCrop clip.
- biltoo-2035: Stage 3 — packPosesMasonry / packPosesMasonryRows pure data plane.
- biltoo-2036: Stage 3 — packPosesFlow (Flow / FlowFill) pure data plane.
- biltoo-2037: Stage 3 — packPosesFacing pure data plane (cover + pairs).
- biltoo-2038: Stage 3 — packPosesMasonryFill / MasonryRowsFill pure data plane;
  all GalleryLayout pack modes now pure-pose + apply.
- biltoo-2039: Stage 3 residual — pack() single switch + apply loop (DRY).
- biltoo-2040: Stage 2 residual — crop record/afterSt/apply seed use freezeItemAppearance;
  Stage 3 GalleryLayout pack marked complete.
- biltoo-2041: Stage 3 residual — packPosesForMode pure dispatcher shared by pack + tests.
- biltoo-2042: Stage 2 residual — all freezes via freezeItemAppearance except crop enter;
  captureState only definition, freeze fallback, crop enter.
- biltoo-2043: Docs — Stage 4a exit criteria checked complete; Stage 4b readiness
  checklist (format version still blocked on product decision).
- biltoo-2044: Stage 4b prep — sparse-only after fat remove characterization;
  getAppearance/hasAppearance marked non-authority for store reads.
- biltoo-2045: Stage 2 residual — clearWorkspace clears ItemWorld sparse tables
  when SessionDocument::clear wipes fat-only store (stale hasDurableAppearance).
- biltoo-2046: Stage 2 residual — setPaths clears fat appearance; applyExpandedLoad
  clears ItemWorld sparse so Open/Replace cannot keep orphaned prior-session rows.
- biltoo-2047: Tests — setPaths_clearsAppearance / replaceAll_keepsAppearance.
- biltoo-2048: Stage 2 residual — Image-mode session remove drops ItemWorld
  appearance (was only Workspace/Gallery via removeWorkspaceSessionId).
- biltoo-2049: Stage 2 residual — SessionDocument::removeAt drops fat appearance
  for the removed id (defense in depth; sparse still via ItemWorld).
- biltoo-2050: Docs — session appearance lifecycle table (Open/Sort/Remove/Wipe).
- biltoo-2067: Stage 2 residual — refresh sessionIndex cache from document after setSessionId.
- biltoo-2078: Stage 2 residual — `refreshSessionIndexCache` consolidates list-order
  cache stamp after setSessionId; bind/LoadAdd no longer gate on cache==-1.
- biltoo-2079: Stage 2 residual — Gallery/Workspace placeholders stamp sessionIndex
  from document (refreshSessionIndexCache), not pack row index.
- biltoo-2080: Stage 2 residual — ItemWorld applied ContentXform runtime table;
  dual-write on syncLive/clearLive; remove/clearAppearance drop rows.
- biltoo-2081: Stage 2 residual — itemHas/itemAppliedContentXform prefer ItemWorld
  when bound; ImageView/pipeline/crop readers migrated; paint keeps item mirror.
- biltoo-2082: Stage 2 residual — CropSession pickApply/canKeep/classify/cropBasis
  take applied from ImageView helpers (no ImageItem applied dig).
- biltoo-2083: Stage 2 residual — setItemSessionId migrates applied→ItemWorld on
  bind; bindSelectedSessionIds uses setItemSessionId; pipeline stash uses helpers.
- biltoo-2084: Stage 2 residual — all valid-id binds route through setItemSessionId
  (load/gallery/canvas/place/cursor); unbind keeps setSessionId(invalid).
- biltoo-2085: Stage 2 residual — itemLiveColor helper for live grade reads;
  durable Color stays ItemWorld; setSessionId docs valid-id via setItemSessionId.
- biltoo-2086: Stage 2 residual — CropSession canKeep takes liveColor from view;
  MainWindow panel uses itemLiveColor.
- biltoo-2087: Stage 2 residual — pure characterization applied ContentXform
  survives pathOrderClear; demotion status table through 2086.
- biltoo-2088: Stage 2 residual — syncLiveColorFromState patches applied
  ContentXform.colorAdjust (item + ItemWorld) for paint/tile LOD coherence.
- biltoo-2089: Stage 2 residual — refreshSessionIndexCache clears sessionIndex
  when unbound; demote duplicate id via setItemSessionId(invalid).
- biltoo-2090: Stage 2 residual — applied colorAdjust test coverage;
  demotion status / sequencing through 2089 (host residual largely complete).
- biltoo-2091: Stage 2 residual — ItemWorld liveColorLag runtime table; itemLiveColor
  prefers it when bound; dual-write on syncLiveColor / bind seed.
- biltoo-2092: Stage 2 residual — pure characterization liveColorLag survives
  pathOrderClear; demotion status through 2091.
- biltoo-2093: Stage 2 residual — paint chrome/tile LOD prefer ImageView
  itemAppliedContentXform / itemLiveColor via live*ForPaint helpers.
- biltoo-2094: Stage 2 residual — demotion status / sequencing through 2093;
  ImageItem reader docs for host vs paint helpers.
- biltoo-2095: Phase 6 Tier 4 residual — pure ViewFraming characterization;
  ImageView harness applied ContentXform + liveColorLag survive pathOrderClear.
- biltoo-2096: Phase 6 Tier 4 residual — soft install characterization (fixture
  PNG SoftPreview, no async wait); ViewFraming defaults on offscreen construct.
- biltoo-2097: Phase 6 Tier 4 residual — ViewTransform pure fit/pad; harness
  prepareImageModeCanvas + fitItem framing scale asserts.
- biltoo-2098: Phase 6 Tier 4 residual — sticky pan capture/restore + preserved
  view scale characterization after fitItem.
- biltoo-2099: Phase 6 Tier 4 residual — cross-session soft install + fit/capture
  restore framing handoff (no async Image LoadReplace).
- biltoo-2100: Phase 6 Tier 4 residual — Image-mode sync LoadReplace navigate
  (installImageModeReplaceItem) + sticky; ItemWorld survives across ids.
- biltoo-2101: Fix characterization harness — imageitem.h include; restoreStickyPanAnchor
  public on host pipeline (pair with captureStickyPanAnchor).
- biltoo-2102: Docs — Phase 6 Tier 4 non-async residual complete through 2101;
  characterization harness status + pack-order policy wording.
- biltoo-2103: Stage 2 residual — itemLiveColor prefers ItemWorld liveColorLag
  when bound (complete 2091 host read path).
- biltoo-2104: SESSION residual — drop path-only filmstrip sessionAppearanceChanged
  / sessionCropApplied overloads (id-keyed only).
- biltoo-2105: SESSION residual — sort/append/remove/slideshow prefer SessionImageId
  over paths().indexOf first-match.
- biltoo-2106: SESSION residual — itemSessionIds + workspace filmstrip selection
  restore prefers SessionImageId after append.
- biltoo-1990: ItemWorld/ImageItem authority docs; sparse-read + private mutators status.
- biltoo-1991: content-edit marks private on ImageItem; ImageView-only host API.
- biltoo-1992: ItemWorld::appearanceValue sparse-prefer; sessionAppearanceValue thin wrapper.

### Metrics at tip 2131 / 2132 (2026-09-21)

| Metric | Phase 6 exit target | Tip 2131 |
|--------|--------------------:|---------:|
| `imageview.h` | < 400 | 711 |
| `imageview*.cpp` total | ~3–4k ideal | ~11.9k |
| Largest `imageview_*.cpp` | < 800 | appearance 901, canvas 885 |
| Controllers | — | Slideshow / Crop / Attention / DisplayPipeline / Gallery / Workspace / Image |
| `SessionDocument` | paths∥ids owner | yes (`MainWindow::m_session`) |
| Phase 7 Stages 0–4b | complete | complete |
| `captureState` call sites | thin | crop enter + freeze fallback + definition |

**Phase 7 residual (not blocking):** ImageItem remains intentional render proxy
(pixmap, live pose, dual-written applied ContentXform mirror for detached tiles).
`WorkspaceItemState` remains the project/clipboard DTO assembled from sparse
tables. Stage 5 dense storage is optional/never unless profiled.

**Phase 6 residual (not blocking):** optional full async PreferCache / thumtoo
ladder under Gallery scroll; Runtime QA of identity series (SESSION §7).

**biltoo-2132:** `SessionDocument::indexOfPathPreferId` — path→list index with
id preference lives on the document (MainWindow is a thin forward). Closes the
SESSION residual “path index only for fully unbound rows” at the document API.

**biltoo-2133:** `indexOfPathOccurrence` / `indicesForPathsByOccurrence` on
SessionDocument; MainWindow remove + append-chrome path fallbacks call them.

**biltoo-2134:** Split appearance TU (core / color grade / crop appearance);
`SessionDocument::indicesForIds`; appearance core 595 lines (under 800).

### ImageItem demotion status (through biltoo-2093)

**ImageItem is a render / hit-test proxy.** Durable content and list identity live
in ItemWorld / SessionDocument. Public surface is readers, interaction handlers,
and view-driven chrome paint. **All mutators are private** (friends: ImageView,
DisplayPipelineController, CropSession, GalleryController, GalleryLayout helpers).
Completed: path/session stamps, interactive/chrome flags, cell size, placement,
content-edit marks, gallery scroll cache (1985–1995); Stage 2 residual store-read
hygiene (2022–2025); applied ContentXform runtime table + host helpers (2080–2084);
live grade via `itemLiveColor` (2085–2086); applied colorAdjust dual-sync (2088);
sessionIndex unbound clear (2089); ItemWorld liveColorLag host scratch (2091);
paint chrome/tile LOD prefer host helpers via `live*ForPaint` (2093).

| Concern | Authority |
|---------|-----------|
| Crop / orient / grade / attention | ItemWorld sparse tables; read via `sessionAppearanceValue` / `appearanceValue` |
| Applied ContentXform (mid-edit) | ItemWorld when bound; host `itemAppliedContentXform`; paint `liveContentXformForPaint` (2093); item mirror dual-written |
| Live colour grade (slider lag) | Host `itemLiveColor` → liveColorLag when bound (2091); paint `liveColorForPaint` (2093); durable is ItemWorld Color |
| Store presence | `hasDurableAppearance` (fat **or** sparse) — not fat-only `hasAppearance` |
| Live pose | `placement()` reader; `applyPlacement` private (+ `GalleryLayout::applyItemPlacement`) |
| Interaction snapshot | `captureState` = store seed + live pose / applied ContentXform / grade |
| Pixels / intrinsic / path / session stamps | Host / pipeline private mutators; valid ids via `setItemSessionId` |
| List order | `sessionListIndex` (document); `refreshSessionIndexCache` clears unbound (2078–2089) |

Dual-write on store **writes** remains so fat DTO serialization stays aligned;
store **reads** prefer sparse via the choke point. Stage 4a save/load/clipboard
build DTOs from sparse-prefer reads; Stage 4b may drop dual-write when the
project format no longer needs the fat DTO mirror.

Freeze policy is `freezeItemAppearance` (2029–2042): store + live when durable
and not mid-edit; else `captureState`. Direct `captureState` call sites (2042):
definition, freeze fallback, crop-enter undo baseline only.

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
