<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Mode ownership (Gallery / Workspace / Image)

## Source of truth for *which images exist*

| Structure | Owner | Role |
|-----------|--------|------|
| `MainWindow::m_session` (`SessionDocument`) | MainWindow | Ordered **session rows**: path + `SessionImageId`. Filmstrip mirrors this. |
| Filmstrip `m_files` / `m_sessionIds` | ThumbnailBar | View of the session list only — never a second membership model. |

**Gallery** must pack **every** session row. **Workspace** places a subset (or
duplicates) of those rows on a free-form canvas. Placing on Workspace must
**not** remove a row from `m_session`.

## Who owns `ImageItem*` pointers

| Structure | Mode | Role |
|-----------|------|------|
| `ImageView::m_items` (`liveItems()`) | Active mode only | Items currently on the `QGraphicsScene`. |
| `WorkspaceController::m_stashedItems` | Left Workspace | Pointer stash of free-form tiles for fast return. **Do not remove** an entry when opening Image — Image mode must not steal Workspace ownership. |
| `WorkspaceController::m_savedItems` | Durable | `WorkspaceItemState` snapshots (pose + appearance ids). Used by `LoadRestore` when the pointer stash is empty. |
| `GalleryController::m_stashedItems` | Gallery → Image only | Pointer stash of packed cells for fast return to Gallery. **Not** used when leaving to Workspace (pack is discarded and rebuilt). |
| Image underlay | Image | Exactly one (or zero) live `ImageItem` for `classicPath` / current session id. Built by load pipeline; **not** by taking a Workspace stash pointer. |

### Forbidden

- Taking an `ImageItem*` out of `m_stashedItems` to become the Image underlay
  (leaves a hole in the Workspace arrangement on return; partial stash skips
  durable `LoadRestore`).
- `setWorkspacePaths(selectedOnly)` while in Gallery (dooms non-selected rows).
- Stashing Gallery packs into Workspace stash (grid layout on free-form canvas).

## Mode switch contract

`ImageView::setViewMode` / `enterGallery`:

1. **Leave** previous (snapshot / stash or discard pack).
2. **`setActiveMode`** so `isXMode()` matches the destination.
3. **Enter** destination with explicit previous mode.

| From → To | Leave does | Enter does |
|-----------|------------|------------|
| Workspace → Image | `snapshot` + **stash all** free-form tiles (pointers stay in Workspace stash) | Clear live; `loadImage(classicPath)` underlay from LQIP / ImageCache / filmstrip sample / tiles |
| Image → Workspace | **Central detach** destroys Image underlay | Restore Workspace pointer stash; if empty, durable `LoadRestore` |
| Gallery → Image | Stash packed cells | `loadImage`; return restores Gallery stash |
| Gallery → Workspace | **Stash pack** (Gallery stash only) | Restore Workspace stash / durable |
| Workspace → Gallery | Stash free-form | Restore Gallery stash if present; else populate |
| Image → Gallery | **Central detach** destroys Image underlay | Prefer Gallery pointer stash; else **`populateGalleryCanvas`** |

### Gallery empty defense (Workspace → Gallery)

1. `setViewMode(Gallery)` leaves Workspace (stash only — do not steal tiles).
2. `GalleryController::enter` never stashes Workspace while mode is still
   Workspace (that legacy path packed Gallery cells into the free-form stash).
3. `MainWindow::populateGalleryCanvas` cancels size-resolve, sets full session
   paths, then **`ensurePlaceholders`** if the canvas is empty **or** every live
   tile is invisible (defer hide). Size-resolve may re-arm after cancel; the
   placeholder pass must still run so the overview is never blank.

## Pixel layers (not “soft ladder”)

Product decision (see `docs/GALLERY_PIXELS.md`, `docs/IMAGE_MODE_NAV_SOFT.md`):

| Layer | Role |
|-------|------|
| LQIP (≤96) | Placeholder only if **already** in process/Store cache — never a standalone soft encode for underlay |
| ImageCache / filmstrip host sample | In-process stand-in if already decoded |
| **Tiles** | Sharpness (Tile LOD) |
| PreferCache / full | Climb when tiles cannot cover need |

Whole-frame soft PreferCache for Gallery/Image underlay is **removed**. Do not
reintroduce “soft worker” as the primary Image open path; use cache + tiles.

## Image open from Workspace (correct)

1. Pin `classicPath` + current `SessionImageId` before leave.
2. Workspace `onLeave`: snapshot + stash (**keep** every pointer in stash).
3. Image `enter`: clear live; seed `ImageCache` from stash **display pixels** if
   any (copy, do not move the item); `loadImage(path)` → LQIP/host sample +
   tile climb.
4. Return to Workspace: `restoreStashedItems()` still has every tile.

## Structural invariants (enforced)

1. **Session membership ≠ canvas.** `m_session` is the only list of which images
   exist. Gallery pack is a *view* of the session and may be rebuilt. It must
   never be emptied by hiding tiles under size-resolve or by destroying stashed
   pointers from another mode.
2. **Mode stashes are exclusive owners of off-scene `ImageItem*`.**
   `clearLiveCanvas` must not `delete` a pointer still listed in Workspace or
   Gallery `m_stashedItems` (assert + skip).
3. **Size-resolve must not `setVisible(false)` on live Gallery tiles.** Defer
   only *creation* of new provisional cells; existing/restored tiles stay visible.
4. **Image mode always has an underlay for non-empty `classicPath`.** After
   `loadImage`, if `itemCount()==0`, force placeholder + frame.
5. **Never `prepareImageModeCanvas` after an Image underlay exists** — it zeros
   `sceneRect` and leaves the view blank.

## Tile grid vs content rect

Tile LOD plans in **file-native** pixel space. `contentRect` / intrinsic size
are **layout** (oriented). `tileNativeSize()` must not fall back to oriented
`imageSize()` when ContentXform has quarter-turns or flips — that paints
unrotated tile patches inside a rotated layout box.

## One scene, not three

Biltoo uses **one** `QGraphicsScene` on `ImageView` on purpose:

| Layer | Where it lives |
|-------|----------------|
| Session membership | `SessionDocument` (paths + `SessionImageId`) |
| Content appearance / crop | `ItemWorld` sparse tables (ECS-style) |
| Workspace poses | Workspace durable snapshot + pointer stash |
| Gallery pack | Ephemeral view of the session (rebuildable) |
| **On-screen items** | Single scene = **active mode presentation only** |

Three scenes would isolate transforms and items, but would also triplicate
selection, scrollbars, chrome, and tile-LOD host wiring, and force
cross-scene reparenting on every switch. Isolation is the **mode switch
pipeline**, not the number of scenes:

1. Snapshot previous presentation into mode **data** stores (stash / durable).
2. **Detach live** (`clearLiveCanvas`) — nothing from the previous mode may remain.
3. Set active mode flag.
4. Attach destination presentation (stash restore, `loadImage`, or full Gallery pack).

Image underlay is presentation-only. It is not stashed; step 2 destroys it.
Forgetting that step (controller-only leave, no Image path) is what put the
ImageView image onto Workspace.

## SessionImageId vs live tile

`SessionImageId` is identity for appearance and session membership. It is **not**
a guarantee of a single `ImageItem*` process-wide.

- **Live canvas** (`m_items`): active-mode presentation only.
- **Workspace stash**: free-form tiles kept while in Image/Gallery.
- **Gallery stash**: packed cells kept while in Image.

`findItemBySessionId` searches **live only**. Gallery pack must create a live
cell for every session row even when Workspace stash already holds that id.
Session remove uses `collectItemsForSessionId` (live + both stashes).


