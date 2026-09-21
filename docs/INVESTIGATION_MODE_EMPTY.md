<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Investigation: empty ImageView / Gallery tiles disappear

Status: **2026-09-21** (tip **2160**). No guesswork — code paths only.

## How to reproduce diagnostics

```bash
BILTOO_MODE_DEBUG=1 BILTOO_LOAD_DEBUG=1 biltoo /path/to/images
```

Stderr lines:

| Prefix | Source |
|--------|--------|
| `biltoo/mode` | Mode switch, Gallery populate, size-resolve cancel |
| `biltoo/load` | `pendingTile`, soft install, placeholder create |

## Observed call chains (static)

### A. Workspace → Image (double-click tile)

1. `MainWindow::openSessionIndexInImageMode`
   - `setClassicPath(path)` + `setCurrentSessionId`
   - `GalleryController::leaveForImageMode` → `setViewMode(Image)`
2. `ImageView::setViewMode(Image)`
   - `WorkspaceController::onLeave(Image)` → `snapshot()` + `stashItems()`
     (live → Workspace stash, off-scene; **pointers kept**)
   - `ImageController::enter()`
3. `ImageController::enter`
   - `setActiveMode(Image)`
   - `prepareImageModeCanvas()` — empty sceneRect, fit-only (OK **before** items)
   - seed `ImageCache` from stash display/pixmap (copy only)
   - `clearLiveCanvas()` + `clearSceneKeepingStashes()`
   - `loadImage(path)` → `scheduleImageLoad(LoadReplace)` → `installImageModePendingTile`

### B. Cold `installImageModePendingTile` (no soft/LQIP yet)

**Bug found (fixed 2160):** when `pixels.isNull()` and `liveItems` empty:

```text
createPlaceholderItem(path, sz)
resetImageModeItemPlacement(item)
prepareImageModeCanvas()   // ← RESET sceneRect to QRectF() AFTER item exists
return                     // ← no fitItem / no applyImageModeFraming
```

Result: underlay may exist in `m_items` but view has **empty sceneRect** and
no framing. Looks empty until a later soft install; if soft never arrives
(tile-LOD-only Workspace tile, no LQIP), stays blank.

**Fix:** `syncImageModeSceneRect` + `applyImageModeFraming`; never
`prepareImageModeCanvas` after the item is created.

### C. Gallery size-resolve hide

`setWorkspacePaths` when size-resolve starts:

```text
setDeferPopulate(true)
for each live item: setVisible(false)
return without creating new tiles
```

`onSizeResolveGateComplete` unhides / `ensurePlaceholders` / packs.

**Bug found (fixed 2160):** `onSizeResolveGateCancelled` only cleared defer.
Hidden tiles stayed **invisible** → “Gallery images disappeared” after any
cancel (mode leave mid-resolve, populate cancel race).

**Fix:** on cancel in Gallery mode: `setVisible(true)` on hidden items; if
still empty and path order non-empty, `ensurePlaceholders`.

### D. Workspace → Gallery

1. `enterGalleryMode` → `enterGallery` → `setViewMode(Gallery)`
   - Workspace `onLeave` → stash free-form
   - Gallery `enter(previous=Workspace)` → clear residual live; **no pack**
2. `populateGalleryCanvas`
   - cancel size-resolve, `setWorkspacePaths(session)`
   - ensure placeholders if empty or all invisible (2159)

## What was ruled out (code evidence)

| Hypothesis | Evidence against |
|------------|------------------|
| Image steals Workspace stash pointer | `ImageController::enter` only seeds ImageCache; does not remove stash entries (2158) |
| Gallery::enter re-stashes Workspace while still Workspace | Legacy bypass removed (2159) |
| `createPlaceholderItem` blocked in Image by defer | Guard is `isGalleryMode() && isDeferPopulate()` only |
| `clearLiveCanvas` deletes stash | Stash off `m_items`; `clearSceneKeepingStashes` keeps stash set |

## Still open (needs runtime log)

1. `classicPath` empty on some Image enter path → log `Image::enter EMPTY classicPath`.
2. Soft worker never delivers for some archive/page paths → `pendingTile PLACEHOLDER` then no `INSTALLED`.
3. Gallery tiles created but pack leaves them off-view → check `applyLayout` after ensure.

With `BILTOO_MODE_DEBUG=1`, a failing switch should print `live=0` after
`Image::enter done` or `populateGallery live=0`. That line is the next evidence
pin, not more theory.
