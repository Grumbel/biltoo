<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# QImgView structural refactor

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

Phases 1–4 (and follow-ups 2b/2c/3b/4b) are complete. Remaining work is either
product features (TODO.md), optional Phase 5 after QA, or Workspace placement
by id (new model work — not part of early-phase exit criteria).
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
