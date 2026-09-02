# Image identity, duplication, and editing

Related: DOMAIN.md, AUDIT.md (H2p / M16 / M27), HANDLES.md.

---

## 0.1 Implementation identity key

**`SessionImageId` (`qint64`, never zero, never reused)** is the stable id of a
session image. List index is order only and may change on insert/delete/sort.
Canvas items and appearance maps bind by `SessionImageId`.

## 0. Intended mental model (product)

This is the model to implement against. Everything below §1 is how the *code*
behaves today and where it diverges.

1. **Session** contains an **ordered list of images**.
   - Each entry is a first-class **session image** (slot index `0 … n-1`).
   - A session image always names a **file on disk** (path), but identity is
     the **slot**, not the path string.
   - **The same file on disk may appear in the session multiple times**
     (multiple slots, same path). Those are independent images in the session.

2. **Gallery** modes show the session images under various **layouts**
   (grid, masonry, …). The gallery is a view of the session list, not a
   separate object store.

3. **Image mode** focuses on **one session image** (the session cursor) and is
   where **editing** of that image’s appearance happens (crop, flip, content
   rotate, etc.).

4. **Workspace** is a **free-form layout** that may place session images on a
   canvas.
   - Placing an image on the Workspace is **optional**.
   - Every object on the Workspace is **associated with exactly one session
     image** (that slot). It is a presentation of that session image, plus
     Workspace-only placement (position, scale, free tilt, opacity, z).
   - Removing a tile from the Workspace does not remove the session image;
     removing a session image removes the association.

5. **Disk file** is only the decode source. Two session images that share a
   path still have independent crop / flip / content rotation. Editing one
   must not change the other.

**Corollary:** APIs keyed only by path cannot be the source of truth for
appearance once path duplicates exist. Session index (or an equivalent stable
id per session image) must be.

---

## 1. Three layers in the current code

The code uses these three layers. Crop/flip/rotate bugs almost always come
from treating **path** as identity when **session slot** should be.

### 1.1 Filesystem path (`QString`)

- Absolute or as-opened path string of an image file on disk.
- **Not** canonicalized (`QFileInfo::canonicalFilePath` is not applied).
- Same file opened via two different strings is two different paths.
- Same path string may appear **more than once** in the session list.

### 1.2 Session slot (`int` index into `MainWindow::m_files`)

- `MainWindow::m_files` is an **ordered** `QStringList`.
- Index `i` means “the i-th entry in the user’s working set”.
- **Duplicates are first-class session entries**: `Ctrl+D` appends another
  copy of the same path string at a new index (does not go through
  `appendFiles()`, which deduplicates).
- Navigation cursor: `MainWindow::m_currentIndex` +
  `ImageView::setSessionPosition(index, total)`.
- Filmstrip rows are 1:1 with session indices (`ThumbnailBar::m_files`).

**Domain wording:** DOMAIN calls the session “an ordered list of image paths”.
Implementation allows the same path string at multiple indices; that is how
Workspace “duplicate into session” is represented.

### 1.3 Canvas object (`ImageItem *`)

- Live `QGraphicsPixmapItem` subclass on `ImageView`’s scene.
- Holds **decoded pixels** (`m_source` / pixmap) and **transforms**.
- Binding to a session slot: `ImageItem::m_sessionIndex`
  - `>= 0` → bound to that session index
  - `-1` → unbound (canvas-only, or not yet rebound)

**One path, many canvas objects is allowed** in Workspace (DOMAIN: duplicate
selection → same path, independent transforms). Gallery DOMAIN text still says
“one object per session path”; the session list can still contain path
duplicates, which is an unresolved tension for Gallery packing.

---

## 2. What lives where (appearance vs placement)

### 2.1 On the live `ImageItem` (instance state — value on the object)

| Field | Meaning |
|-------|---------|
| `m_path` | Source file path |
| `m_sessionIndex` | Bound session slot, or -1 |
| `m_source` / pixmap | **Current displayed pixels** (may already include crop/flip/90° bake) |
| `m_sessionHasCrop` / `m_sessionCropRect` | Crop in **on-disk** pixel coordinates (metadata) |
| `m_contentHFlip` / `m_contentVFlip` | Net content flips vs on-disk (after crop), for chrome indicators |
| `itemRotation()` | Placement angle (Workspace free rotate); cleared in Image mode |
| `itemScale` / pos / opacity / z | Workspace placement |
| `itemHFlip` / `itemVFlip` | Legacy **display** flips (normally cleared after bake) |

Crop **Apply** bakes pixels via `cropToLocalRect` and updates session crop
metadata on that item. Flip/90° use `bakeFlip` / `bakeRotate90` and update
content flags via `ImageView::bakeItemFlip` / `bakeItemRotate90`.

### 2.2 `ImageView::m_itemStates` — `QHash<QString, WorkspaceItemState>`

- **Keyed by path only.**
- At most **one** `WorkspaceItemState` per path string.
- Used for: Image-mode reload appearance, path-level filmstrip signals,
  historical “session appearance survives navigation”.
- **Cannot** represent two independent crops/flips for two session slots that
  share a path. Last writer wins.

### 2.3 `ImageView::m_sessionSlotStates` — `QHash<int, WorkspaceItemState>`

- **Keyed by session index.**
- Added to store per-slot appearance as value copies.
- Written in `commitItemSessionEdit` when `item->sessionIndex() >= 0`.
- Read in `createItemFromImage` (Image mode) and stash restore as preferred
  appearance source.
- Still coexists with path-keyed maps; dual write is easy to desync.

### 2.4 `ImageView::m_savedWorkspace` — `QList<WorkspaceItemState>`

- Snapshot of **all** canvas items when leaving Workspace (list, not hash).
- **Can** hold two entries with the same path and different transforms.
- Restored via `LoadRestore` pending queue (AUDIT M27: duplicate paths OK).
- On live edit, `commitItemSessionEdit` also tries to patch matching entries
  by path + sessionIndex.

### 2.5 Thumbnail overrides — `ThumbnailBar::m_sessionImageOverrides`

- **Keyed by path only.**
- `sessionAppearanceChanged` / `sessionCropApplied` emit `(path, image)`.
- Override updates **every filmstrip row** whose path equals that string.
- Two session slots with the same path **cannot** show different thumbs.

---

## 3. Mode rules (canvas population)

| Mode | Canvas content | Identity of “current” |
|------|----------------|------------------------|
| **Image** | At most one `ImageItem` | Session cursor `m_currentIndex` → path `m_files[i]` |
| **Gallery** | One tile per layout path set | Session cursor + tile selection |
| **Workspace** | Zero or more free objects | Subset of session; each may be bound via `sessionIndex` |

### 3.1 Entering Image mode

Common paths:

1. **Filmstrip / `setCurrentIndex(i)`**  
   - Sets cursor to `i`, loads `m_files[i]`.  
   - `ImageView::m_sessionIndex` updated via `setSessionPosition`.  
   - Image-mode `LoadReplace` creates one item and (current tip) sets
     `item->setSessionIndex(m_sessionIndex)`.

2. **`showPathInImageMode(path)`**  
   - Resolves `idx = m_files.indexOf(path)` → **first** occurrence only.  
   - Wrong for path duplicates: always opens the earliest slot.

3. **`sessionSlotOpenRequested(sessionIndex)`** (Workspace double-click when
   item is bound)  
   - Opens that index via `setCurrentIndex(sessionIndex)`.  
   - Correct for bound duplicates **if** the canvas item’s `sessionIndex`
     is still accurate.

4. **Gallery open** still emits path-only `galleryItemOpenRequested(path)`.

While in Image mode, Workspace tiles are **stashed** (`m_stashedWorkspaceItems`):
detached from the scene but kept alive with their pixels and `sessionIndex`.

### 3.2 Returning to Workspace

- Prefer `restoreStashedWorkspaceItems()` (reattach live items).
- Else rebuild from `m_savedWorkspace` / `LoadRestore`.
- Stash restore may rebuild pixels from `m_sessionSlotStates` or path map when
  size/metadata disagree — secondary path; primary intent is that
  `commitItemSessionEdit` already updated stashed peers in place.

---

## 4. Duplication (Workspace)

### 4.1 User action: `MainWindow::duplicateSelected`

1. `ImageView::duplicateSelected()`  
   - For each selected canvas item, `createItemFromImage(path, sourceImage,
     applyStoredSessionCrop=false)` → **new object**, pixels copied as-is.  
   - Copies placement (scale, rotation, opacity, z, offset pos).  
   - Current tip also copies content flip flags and session crop metadata.  
   - New items remain selected; **sessionIndex not set here**.

2. MainWindow appends each source path again onto `m_files` (allows duplicates).

3. `bindSelectedSessionIndices(firstNew)` assigns new indices to the still-
   selected copies and seeds `m_sessionSlotStates`.

4. Filmstrip `setFiles(m_files)`; canvas membership badges refreshed.

**Identity result:** two session slots, same path string, two canvas objects,
each bound to a different `sessionIndex`, each holding its own pixel buffer
(value copy at duplicate time).

### 4.2 Other ways to get a second canvas object with the same path

- `addImageForSession(path, sessionIndex)` if that index is not yet on canvas:
  may clone pixels from `findItemByPath` (first match) with
  `applyStoredSessionCrop=false`, then `setSessionIndex`.
- `placeOrMoveImageAt` / drop: may create another instance at
  `sessionIndex = -1` until rebind.

### 4.3 What duplication does **not** do

- Does not share a pointer to one pixel buffer between original and copy after
  creation (each has its own `QImage` after copy-on-write detach on edit).
- Does not give the filmstrip a per-slot appearance channel (path override only).

---

## 5. Editing pipeline

### 5.1 Who is edited?

| Context | Target |
|---------|--------|
| Workspace chrome / tools | Selected `ImageItem`(s) on the live canvas |
| Image mode tools / crop | The single Image-mode item (`cropTargetItem` / `targetItem`) |
| Gallery rotate/flip | Selected gallery tiles (if enabled) |

### 5.2 Bake path (flip / ±90°)

1. `bakeItemFlip` / `bakeItemRotate90` mutate the **item’s** pixels.
2. Update content flags and **path-keyed** `m_itemStates` (and quarter turns).
3. `commitItemSessionEdit(item)`:
   - `rememberItemState` (Image mode merges into path map carefully; Workspace
     uses `captureState`)
   - If `sessionIndex >= 0`: write `m_sessionSlotStates[sessionIndex]`, emit
     path-keyed filmstrip override
   - **Sync** to other live/stashed/gallery items **only if**
     `other->sessionIndex() == sessionIndex` (same path required), with a
     sole-path fallback for Image mode when no bound peer exists

### 5.3 Crop path

1. Enter crop: load full on-disk image onto the target item; draft rect from
   prior session crop metadata.
2. Apply: `recordSessionCrop` (item metadata + path map if bound),
   `cropToLocalRect` (bake pixels), `commitItemSessionEdit`.
3. Undo (if pushed) restores pre-enter snapshot via `applyCropAppearance`.

### 5.4 Image mode → Workspace propagation (intended)

```
Image-mode item (sessionIndex = N)
        │ commitItemSessionEdit
        ├─► m_sessionSlotStates[N]     (value copy of appearance fields)
        ├─► m_itemStates[path]         (path collapse — last slot wins)
        ├─► filmstrip override[path]   (all rows with path)
        └─► for each stashed/live peer with sessionIndex == N:
                setSourceImage / content flags / session crop  (value copy)
```

**If the Image-mode item’s `sessionIndex` is wrong or -1, stashed Workspace
tiles do not update.** Filmstrip may still change because signals are path-
keyed.

**If the user opened Image mode via path (`indexOf`), they may be editing slot
0 while looking at a duplicate that was slot 1 on the canvas.**

---

## 6. Lookup helpers and their bias

| API | Behavior |
|-----|----------|
| `findItemByPath(path)` | **First** canvas item with that path |
| `findItemBySessionIndex(i)` | Item bound to session index `i` |
| `m_files.indexOf(path)` | **First** session slot with that path |
| `createItemFromImage(..., applyStoredSessionCrop=true)` | Applies appearance from `m_sessionSlotStates[m_sessionIndex]` in Image mode if present, else **path** `m_itemStates` |
| `createItemFromImage(..., false)` | Pixels used as-is (duplicate / donor clone) |

Any code that still uses path-first lookup will mis-handle duplicates.

---

## 7. End-to-end scenarios

### 7.1 Single session entry, one Workspace tile

1. Session `[A]`, canvas item bound to 0.  
2. Image mode crop on index 0 → stash peer 0 updated → return shows crop.  
**Works** if sessionIndex stays 0 through stash/commit.

### 7.2 Duplicate then crop “the copy” in Image mode

1. Session `[A]`, canvas item0 `@0`.  
2. Duplicate → session `[A, A]`, item1 `@1`, item0 `@0`.  
3. Double-click item1 → should emit `sessionSlotOpenRequested(1)`.  
4. Crop apply → commit with sessionIndex 1 → sync only peers `@1`.  
5. Return: item1 cropped, item0 unchanged.

**Breaks if:** open used path (`indexOf` → 0); item1 unbound (`sessionIndex -1`);
sync skipped; stash restore rebuilds from path map and overwrites the wrong
item; or Image load applied path crop from the other slot.

### 7.3 Thumbnail after crop with duplicates

Path override updates **both** rows. Filmstrip cannot show two different
appearances for one path. That matches path-keyed signals, not per-slot
identity.

### 7.4 Workspace-only crop (no Image mode)

Crop target is the selected canvas item. `commitItemSessionEdit` updates that
item and peers with the same sessionIndex. User report: this path “seems to
work” relative to Image mode — consistent with editing the live object
directly without a path-only open step.

---

## 8. Known structural contradictions (do not paper over)

1. **Path-keyed vs slot-keyed appearance**  
   `m_itemStates` and filmstrip overrides are path-keyed; duplicates need
   slot-keyed (or list-keyed) appearance. `m_sessionSlotStates` was added as a
   third store without removing the path store → dual source of truth.

2. **Opening Image mode by path**  
   `showPathInImageMode` / Gallery open / any `indexOf(path)` cannot address
   the second session entry of a duplicated path.

3. **DOMAIN Gallery “one object per path” vs session path duplicates**  
   Unspecified what Gallery shows when `m_files` contains `A` twice.

4. **`findItemByPath` / first-match**  
   Clones and donor lookups always see the first canvas instance.

5. **Pixels vs metadata**  
   Appearance is sometimes “already baked into `m_source`” and sometimes
   “full decode + cropRect + content flags”. Rebuild paths must agree on order
   (DOMAIN/AUDIT: disk → crop → flip → quarter turns).

6. **Unbound canvas objects (`sessionIndex == -1`)**  
   Edits do not write slot maps and do not sync to Image/session. Rebind
   (`rebindWorkspaceSessionIndices`) assigns free slots by path occurrence
   order — order-dependent, not identity-stable across arbitrary operations.

---

## 9. Invariants the implementation must obey (for a correct design)

Derived from DOMAIN + the failure modes above. Not all are held today.

1. **Session index is the identity of an editable “picture instance”** once it
   has been placed in the session list (including duplicates).
2. **Path is only the decode source**, not the key for appearance when the
   same path can appear more than once.
3. **Canvas objects bound to index N** are value holders for that slot’s
   current pixels and placement; editing Image mode at cursor N must update
   the bound object N (including while stashed).
4. **Duplication** creates a new session index and a new canvas object with
   **copied** pixels and metadata, then independent lifetime.
5. **Filmstrip row i** reflects session slot i (appearance override must be
   index-keyed if rows can share a path).
6. **No path-keyed hash may be the sole store** of crop/flip/90° if duplicates
   are supported.

---

## 10. Map of primary code sites

| Concern | Location |
|---------|----------|
| Session list | `MainWindow::m_files`, `setCurrentIndex`, `duplicateSelected` |
| Mode switch / stash | `ImageView::setViewMode`, `stashWorkspaceItems`, `restoreStashedWorkspaceItems` |
| Item create + crop apply on decode | `ImageView::createItemFromImage` |
| Edit commit / sync | `ImageView::commitItemSessionEdit` |
| Flip / 90° | `bakeItemFlip`, `bakeItemRotate90` |
| Crop UI | `setCropMode`, `leaveCropModeInternal`, `recordSessionCrop` |
| Workspace duplicate canvas | `ImageView::duplicateSelected` |
| Bind slot after session append | `bindSelectedSessionIndices` |
| Open Image from Workspace | double-click → `sessionSlotOpenRequested` / `galleryItemOpenRequested` |
| Filmstrip override | `ThumbnailBar::setSessionImageOverride` |
| State types | `WorkspaceItemState` in `imageview_types.h` |

---

## 11. What “fixed” means (acceptance, not a patch plan)

For **two session slots with the same path**, both on the Workspace canvas,
bound to indices 0 and 1:

- Opening Image mode from tile 1 edits slot 1 only.
- Crop/flip/90° in Image mode updates tile 1’s pixels after return; tile 0
  unchanged.
- Workspace-only crop on tile 1 does the same without affecting tile 0.
- Filmstrip row 1 reflects tile 1’s appearance; row 0 reflects tile 0’s.
- Leaving and re-entering Workspace preserves both appearances independently.

Until those hold without path-first shortcuts, duplicate identity is incomplete.


---

## 12. Handoff status (see SESSION.md)

Implementation now keys appearance and peer sync on **`SessionImageId`**.
Path-keyed `m_itemStates` remains a legacy last-writer cache only.

**Do not** reintroduce:

- `clearWorkspace()` on Image-mode LoadReplace (use `clearLiveCanvas()`)
- Peer sync by path or by `m_currentSessionId` in Workspace
- Crop prior rect from path map when a session id is bound
- Thumbnail canvas membership by path occurrence (use `detachCanvasSessionId` /
  `addImageForSession` with `SessionImageId`)
- Writing bound-tile content appearance into the path map (`rememberItemState`)

Full residual list and bundle index: [SESSION.md](SESSION.md) (including
continuation for project/export/controllers).


---

## 13. Crop identity (must not leak)

**Symptom (fixed):** applying crop changed unrelated Workspace tiles / filmstrip
rows that shared a path or happened to be `m_items.first()`.

**Rules:**

1. Entering crop locks **`m_cropTargetItem` + `m_cropTargetId`** for the whole
   session. `cropTargetItem()` must return that lock while `m_cropMode` is true.
2. **Never** re-resolve the crop subject via `targetItem()` / selection or
   `primaryItem()` (`m_items.first()`) during crop mode.
3. Workspace **never** falls back to `primaryItem()` for crop when selection is
   empty — require a single selected target.
4. `recordSessionCrop` / `commitItemSessionEdit` write appearance **only** under
   `SessionImageId`. Do not use `m_currentSessionId` as a stand-in on a multi-tile
   Workspace canvas.
5. Peer sync in `commitItemSessionEdit` matches **`other->sessionId() == sessionId`**
   only (never path).
6. Filmstrip / HUD: emit and handle **`sessionCropApplied(SessionImageId, …)`**
   only. Path-keyed overloads must not repaint every row with the same file.

**Acceptance:** two tiles, same path, different `SessionImageId` — crop tile A;
tile B pixels and filmstrip row B stay unchanged.

---

## 14. SessionImageId allocation (uniqueness)

Implemented in `SessionDocument`:

| Property | Behaviour |
|----------|-----------|
| Type | `qint64`; `0` is **invalid** (`kInvalidSessionImageId`) |
| First id | `m_nextId` starts at **1** |
| Alloc | `allocId()` → `m_nextId++` (monotonic) |
| Remove | drops the id from the list; **does not** return it to the pool |
| Clear session | clears paths/ids; **keeps** `m_nextId` (no recycle after clear) |
| Restore / load | `replaceAll` / `append(..., id)` advance `m_nextId` past any retained id |
| Duplicate | new row gets a **fresh** `allocId()` (never copies the source id) |

Ids are unique **within a process lifetime** of the `SessionDocument`. They are
not recycled on delete, clear, or duplicate. Project load must preserve stored
ids and advance `m_nextId` (already done in `replaceAll`).

**Do not** reset `m_nextId` to 1 on `clear()` without also wiping every
appearance map / canvas bind that could still hold old ids.
