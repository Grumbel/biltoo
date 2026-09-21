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

Crop/flip/rotate bugs almost always come from treating **path** as identity
when **`SessionImageId`** should be. List index is order only.

### 1.1 Filesystem path (`QString`)

- Absolute or as-opened path string of an image file on disk.
- **Not** canonicalized (`QFileInfo::canonicalFilePath` is not applied).
- Same file opened via two different strings is two different paths.
- Same path string may appear **more than once** in the session list.
- Path is the **decode source** only — not appearance or selection identity
  for bound rows (see Migration status below).

### 1.2 Session document row (`SessionDocument`)

- Parallel **`paths()` ∥ `ids()`** lists; each row is a first-class session image.
- **Identity:** `SessionImageId` (`qint64`, never 0, never reused within the
  document lifetime — §14).
- **Order:** list index `0 … n-1` for navigation, filmstrip, and pack; indices
  shift on insert/delete/sort.
- **Duplicates:** `Ctrl+D` / `applyDuplicate` allocates a **new** id and appends
  another row with the same path string.
- Navigation cursor: `MainWindow::m_currentIndex` into the document; resolve
  appearance and canvas tiles by `sessionIdAt(index)` / `indexOfSessionId`.
- Filmstrip is 1:1 with document rows (`setSession(paths, ids)`).
- Path→index last resort: `indexOfPathPreferId` (first bound id for path, else
  `paths().indexOf`).

### 1.3 Canvas object (`ImageItem *`)

- Live `QGraphicsPixmapItem` subclass on `ImageView`’s scene.
- Holds **decoded pixels** (`m_source` / pixmap) and **transforms**.
- Binding to a session image:
  - **Identity:** `ImageItem::sessionId()` (`SessionImageId`; 0 = unbound)
  - **List order:** `ImageView::sessionListIndex(item)` (document when bound)
  - `ImageItem::sessionIndex()` is a **list-order cache** only (may lag after
    reorder; restamped by Gallery pack / rebind)

**One path, many canvas objects is allowed** in Workspace (duplicate selection
→ same path, independent transforms and ids). Gallery pack/reorder prefer ids
(`reorderItemsByPaths(paths, ids)`); path first-unseen is unbound fallback.

---

## 2. What lives where (appearance vs placement)

### 2.1 On the live `ImageItem` (instance state — value on the object)

| Field | Meaning |
|-------|---------|
| `m_path` | Source file path |
| `m_sessionId` | Stable session-image id (0 = unbound) — appearance / peer key |
| `m_sessionIndex` | Deprecated list-order cache only; prefer `sessionListIndex()` |
| `m_source` / pixmap | **Current displayed pixels** (may already include crop/flip/90° bake) |
| Applied `ContentXform` | Live crop/orient fingerprint (mid-edit authority; not a parallel DB) |
| `placement()` | Workspace pose (pos/scale/rotation/opacity/z/item flips) |

Crop **Apply** and orient bake update **ItemWorld** by `SessionImageId` (and
applied ContentXform on the tile). ImageItem is a render / hit-test proxy —
see REFACTOR.md demotion status.

### 2.2 ItemWorld / SessionAppearanceStore (current)

| Store | Key | Role |
|-------|-----|------|
| Session appearance + sparse Crop/ContentBake/Color/Placement | `SessionImageId` | Bound content + pose |
| `PathItemStateBook` | path | Unbound tiles; orient hints for bound (no crop — tips 2009–2013) |

Reads: `sessionAppearanceValue` / `appearanceValue` (sparse-first).
Writes: `setAppearance` / component setters (dual-write fat DTO until Stage 4).

### 2.3 Historical (removed / renamed — do not reintroduce)

- `ImageView::m_itemStates` — path-keyed last-writer appearance (cannot hold
  independent crops for path duplicates)
- `ImageView::m_sessionSlotStates` — index-keyed appearance (index shifts on
  insert/delete; superseded by SessionImageId)

### 2.4 `WorkspaceController` saved items — `QList<WorkspaceItemState>`

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
| **Workspace** | Zero or more free objects | Subset of session; each may be bound via `sessionId` |

### 3.1 Entering Image mode

Common paths:

1. **Filmstrip / `setCurrentIndex(i)`**  
   - Sets cursor to document row `i`, loads that path + `SessionImageId`.  
   - `ImageView` session cursor updated via `setSessionPosition` / host SessionId.  
   - Image-mode `LoadReplace` creates one item bound to the current `SessionImageId`.

2. **Path-only open** (`showPathInImageMode` / similar)  
   - Resolving by path alone is **first occurrence** — wrong for path duplicates.  
   - Prefer id- or index-keyed open APIs.

3. **`sessionSlotOpenRequested(sessionIndex)`** (Workspace double-click)  
   - Opens document row via `setCurrentIndex`.  
   - Prefer the item’s `sessionId` when present; `sessionIndex` cache may lag.

4. **Gallery open** should prefer session id when the tile is bound.

While in Image mode, Workspace tiles are **stashed**: detached but kept alive
with pixels and **`sessionId`** (list-order cache may be refreshed on rebind).

### 3.2 Returning to Workspace

- Prefer restore of stashed items (reattach live tiles).
- Else rebuild from WorkspaceController saved items / `LoadRestore`.
- Peer appearance is already on ItemWorld by `SessionImageId`; stash restore
  must not re-apply path-map crop to bound ids (tips 2009–2013).

---

## 4. Duplication (Workspace)

### 4.1 User action: `MainWindow::duplicateSelected`

1. `ImageView::duplicateSelected()`  
   - For each selected canvas item, create a new tile with copied pixels/placement.  
   - New items remain selected; they receive a **new** `SessionImageId` when
     MainWindow appends a session row (never copy the source id).

2. MainWindow appends each source path again onto the session document (allows
   path duplicates) and allocates a fresh id.

3. Bind selected tiles to the new ids (`bindSelectedSessionIds` / document order).

4. Filmstrip and canvas membership refresh from the document.

**Identity result:** two session rows, same path string, two canvas objects,
each with a **distinct** `SessionImageId` and independent appearance in ItemWorld.

### 4.2 Other ways to get a second canvas object with the same path

- `addImageForSession` / place with a session id: prefer id-keyed lookup
  (`findItemBySessionId`); path-first lookup is first-match only.
- `placeOrMoveImageAt` / drop: new tiles start unbound until document bind
  assigns a `SessionImageId` (list-order cache updated via `sessionListIndex`).

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

1. `bakeItemFlip` / `bakeItemRotate90` update applied ContentXform + pixels.
2. Bound: `ItemWorld::setAppearance` / sparse ContentBake (and path orient hint
   without crop — tip 2010).
3. `commitItemSessionEdit(item)`:
   - Bound: `persistSessionAppearanceSlot` (single `setAppearance` + durable)
   - Unbound: path map via `rememberItemState`
   - Peer sync by **`SessionImageId` only** (never path)

### 5.3 Crop path

1. Enter crop: full on-disk image on the locked crop target (id + item).
2. Apply: store crop on ItemWorld by id (not path for bound), bake pixels,
   `commitItemSessionEdit`.
3. Undo restores pre-enter appearance via ItemWorld / crop session snapshot.

### 5.4 Image mode → Workspace propagation (intended)

```
Image-mode item (sessionId = S)
        │ commitItemSessionEdit
        ├─► ItemWorld appearance[S]     (sparse + fat DTO dual-write)
        ├─► filmstrip override by id    (not path-wide when ids present)
        └─► peers with sessionId == S:  rematerialize / sync appearance
```

**If the Image-mode item has no `SessionImageId`, peers do not update by id.**
Path-only open remains unsafe for path duplicates (prefer id-keyed open).

---

## 6. Lookup helpers and their bias

| API | Behavior |
|-----|----------|
| `findItemByPath(path)` | **First** canvas item with that path (deprecated for identity) |
| `findPreferredItemForPath(path)` | Selected sole match, else sole live match; else nullptr if ambiguous |
| `findItemForPath(path)` | Preferred, else first-match (legacy best-effort bridge) |
| `focusGalleryItem(item)` | Exclusive-select known live item (Gallery keyboard); no path lookup |
| `focusSessionId(id)` | Exclusive-select live item by SessionImageId (filmstrip / session cursor) |
| Gallery `m_focusSessionId` | Return-from-Image restore prefers id over path |
| `findItemBySessionIndex(i)` | Item bound to session index `i` |
| `m_files.indexOf(path)` | **First** session slot with that path |
| `createItemFromImage(..., applyStoredSessionCrop=true)` | Applies appearance from `m_sessionSlotStates[m_sessionIndex]` in Image mode if present, else **path** `m_itemStates` |
| `createItemFromImage(..., false)` | Pixels used as-is (duplicate / donor clone) |

Any code that still uses path-first lookup will mis-handle duplicates.

---

## 7. End-to-end scenarios

### 7.1 Single session entry, one Workspace tile

1. Session row with path `A` and id `S0`, canvas item bound to `S0`.  
2. Image mode crop on `S0` → ItemWorld appearance[`S0`] updated → peers with
   `sessionId == S0` rematerialize → return shows crop.  
**Works** when the open path binds the Image-mode item to `S0` (not a path-only
first match).

### 7.2 Duplicate then crop “the copy” in Image mode

1. Session `[A@S0]`, item0 bound to `S0`.  
2. Duplicate → session `[A@S0, A@S1]`, item1 bound to `S1`.  
3. Double-click item1 → open by id/row for `S1`.  
4. Crop apply → commit writes appearance[`S1`] → sync only peers with `S1`.  
5. Return: item1 cropped, item0 unchanged.

**Breaks if:** open used path (`indexOf` → first row); item1 never received
`S1`; peer sync by path; path-map crop leaked onto `S0` (tips 2009–2013).

### 7.3 Thumbnail after crop with duplicates

Filmstrip overrides must be **id-keyed** when session ids are present. Path-wide
override would paint both rows the same.

### 7.4 Workspace-only crop (no Image mode)

Crop target is the selected canvas item locked by id. `commitItemSessionEdit`
updates ItemWorld for that `SessionImageId` and id-matched peers.

---

## 8. Remaining pitfalls (do not reintroduce)

1. **Path-only open** — `indexOf(path)` cannot address the second row of a
   duplicated path; prefer `SessionImageId` or document index.
2. **`findItemByPath` / first-match** — clones and donors must prefer
   `findItemBySessionId`.
3. **Path-map crop for bound ids** — writes are a no-op at `setPathState`; reads
   must not adopt path crop (IDENTITY path-map rules, tips 2009–2013).
4. **List-order cache** — `ImageItem::sessionIndex` may lag; use
   `sessionListIndex` / document for durable order (tips 1998–2002).
5. **DOMAIN Gallery “one object per path” vs path duplicates** — still a
   product tension; packing must not collapse ids.

---

## 9. Invariants (current design)

1. **`SessionImageId` is the identity** of an editable picture instance
   (including duplicates). List index is order, not identity.
2. **Path is only the decode source**, not the key for bound appearance.
3. **Canvas objects bound to id `S`** hold display pixels + placement; edits
   update ItemWorld[`S`] (including while stashed peers share `S`).
4. **Duplication** allocates a **new** `SessionImageId` and a new canvas object
   with copied pixels, then independent lifetime.
5. **Filmstrip row i** reflects document row i; appearance override is
   id-keyed when ids are present.
6. **No path-keyed store may be the sole authority** for crop when duplicates
   share a path (orient/flip path hints are allowed; crop is not for bound ids).

---

## 10. Map of primary code sites

| Concern | Location |
|---------|----------|
| Session list | `SessionDocument` (MainWindow), `setCurrentIndex`, `duplicateSelected` |
| Appearance | `ItemWorld` / `SessionAppearanceStore`, `sessionAppearanceValue` |
| Mode switch / stash | `ImageView::setViewMode`, Workspace/Gallery controllers |
| Item create + crop on decode | `DisplayPipelineController::createItemFromImage` |
| Edit commit / peer sync | `ImageView::commitItemSessionEdit` (by id) |
| Flip / 90° | `bakeItemFlip`, `bakeItemRotate90` |
| Crop UI | CropSession / crop controllers, locked target id |
| Bind after append | `bindSelectedSessionIds` |
| Open Image from Workspace | id- or index-keyed slot open (not path-only) |
| Filmstrip override | id-keyed when session ids present |
| State DTO | `WorkspaceItemState` in `imageview_types.h` (project + dual-write) |

---

## 11. What “fixed” means (acceptance)

For **two session rows with the same path**, both on the Workspace canvas,
bound to distinct `SessionImageId`s `S0` and `S1`:

- Opening Image mode from tile `S1` edits `S1` only.
- Crop/flip/90° in Image mode updates tile `S1` after return; tile `S0` unchanged.
- Workspace-only crop on tile `S1` does the same without affecting tile `S0`.
- Filmstrip row for `S1` reflects `S1`’s appearance; row for `S0` reflects `S0`.
- Leaving and re-entering Workspace preserves both appearances independently.
- Path-map / XDG never applies **crop** from a shared path onto a bound id.

Regression checklist for agents: no path-only open for the second duplicate;
no path-map crop read/write for bound ids; peer sync by id only.

---

## 12. Handoff status (see SESSION.md / TODO.md)

Implementation keys appearance and peer sync on **`SessionImageId`**.
Path book is for **unbound** tiles and orient/flip hints only (see Appearance
ownership table above).

**Do not** reintroduce:

- `clearWorkspace()` on Image-mode LoadReplace (use `clearLiveCanvas()`)
- Peer sync by path or by “current session id” on a multi-tile Workspace canvas
- Crop prior rect from path map when a session id is bound
- Thumbnail canvas membership by path occurrence (use id-keyed APIs)
- Writing bound-tile **crop** into the path map (`setPathState` is a no-op; do
  not bypass)
- Treating `ImageItem::sessionIndex` as identity (list-order cache only)

Latest tip / bundle index: [TODO.md](TODO.md). Broader session notes: [SESSION.md](SESSION.md).

**Implementation status (2104–2125):** bound tiles use SessionImageId for
placement, open/focus/remove/reveal, reorder, and path→index resolution.
Path remains last-resort for fully unbound rows and decode-only uses.
PreferCache host rules: [SIZE.md](SIZE.md).



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


---

## Migration status (SessionImageId)

**Done (do not regress):**
- Appearance store `m_appearance` keyed by SessionImageId for bound tiles
- Filmstrip `RoleSessionId` + mime `application/x-biltoo-session-ids`
- Drop / place prefer session id; drop-duplicate allocates a new id
- Peer crop sync by SessionImageId only (never path)
- Uniqueness checks within live / stash lists
- Session row order: `SessionDocument` (MainWindow) + Gallery-local `SessionPathOrder` (`m_pathOrderBook`) for LoadAdd multiplicity — see [docs/PATH_ORDER.md](docs/PATH_ORDER.md)
- placeOrMove / LoadAdd append view-book rows; identity ids come from the document when bound

**Path is still used only for:**
- Decoding bytes from disk
- External DnD URI lists
- Legacy `m_itemStates` for *unbound* tiles (no SessionImageId yet)

**Do not add** new path-keyed identity, appearance, or selection APIs.


### Appearance ownership (standing, tip 873+; path-map IDENTITY through 2013)

| Authority | Key | Notes |
|-----------|-----|-------|
| `SessionAppearanceStore` / ItemWorld sparse | `SessionImageId` | Sole live content appearance for **bound** rows (crop included) |
| `SessionDocument` | index | Paths + ids only — no crop/flip/turns |
| Filmstrip id override | `SessionImageId` | Derived; never path-wide when ids present |
| Thumtoo path XDG appearance | path | Orient/flip/grade hint only; **never crop** for bound `SessionImageId` |
| `PathItemStateBook` (`setPathState`) | path | **Unbound** tiles only: full state including crop. **Bound** writes are a no-op (`ItemWorld::setPathState`); content+pose are sparse + XDG |

**Do not:**
- Bake path-keyed XDG into filmstrip cells that have session ids
- Use path as the write key for crop after Apply on a bound id
- Merge path-map **crop** into a bound slot on restore, soft-paint, or slideshow snapshot (orient/flip only)

Call sites that must stay aligned: workspace snapshot/restore (2009–2012), bakeRotate path slot (2010), `imageWithSessionAppearance` (2012), slideshow content snapshot (2013).



---

## 15. Content id vs variant id (design note)

SessionImageId is the **variant** layer (one concrete edited instance). The
**content** layer is the source bytes (thumtoo `content_id` / SHA-256, or path
until hashed).

Do not mix them: non-destructive crop/flip/grade must not invent a new content
hash; export of a derivative may. Filmstrip, appearance, and canvas bind stay
on SessionImageId; “same file / duplicates / tags” stay on content id.

Full brainstorm, schemes, and rules of thumb: [CONTENT-VARIANT.md](CONTENT-VARIANT.md).
