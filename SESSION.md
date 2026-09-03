# Session handoff — identity, chrome, crop (bundles 001–024)

**Tip ref (identity series):** `ae3043f` — see **Continuation handoff** below for later tip `b1fde01`  
**Apply order:** `qimgview-001-…` through `qimgview-024-…` from artifacts, each
`git pull <bundle> HEAD` in sequence. Base is upstream `Grumbel/qimgview` tip
at the time the series started.

Related docs: [DOMAIN.md](DOMAIN.md), [IDENTITY.md](IDENTITY.md), [HANDLES.md](HANDLES.md),
[AUDIT.md](AUDIT.md), [TODO.md](TODO.md).

---

## 1. Product model (agreed)

- **Session** = ordered list of **session images**.
- Each **session image** has a **stable id** + a **disk path** (decode source).
- **Same path may appear multiple times**; those entries are independent.
- **Gallery** = layouts over the session list.
- **Image mode** = focus + **content edit** one session image (crop, flip, 90°).
- **Workspace** = optional free placement of session images; each tile ↔ one
  session image (plus pos/scale/free tilt/opacity/z).

Identity key: **`SessionImageId` (`qint64`, never 0, never reused after remove).**  
List **index** is order only (navigation, filmstrip row) and may shift.

---

## 2. What landed in this series (by theme)

### Workspace chrome / handles (early bundles ~001–012 area)

- Opacity slider vertical on the **left**; constant size; bottom-anchored when
  the frame is tall enough, otherwise packed under the rotate handle (same
  pattern as other chrome).
- Vertical cursor on the opacity track.
- Flip chrome reflects **content** flip state after baking.
- Bolder, shared handle style (line–arc–line corners); hover grow.
- Group selection: rigid rotate of the whole group.
- Crop handles aligned with Workspace style; Reset/Apply outside bottom,
  clamp inside viewport if off-screen.
- Middle-mouse pan allowed in crop mode.
- Crop undo command.
- Free zoom/pan: no selection-chasing on zoom.
- Qt `QImage::mirrored` → `flipped`.

### Session identity (014–019, 024)

- **`SessionImageId`** type in `imageview_types.h`.
- `MainWindow::m_sessionIds` parallel to `m_files`; alloc on load/append/dup;
  remove/sort keep pairs aligned.
- `ImageItem::sessionId`, `m_sessionAppearance[id]`.
- Image open from Workspace: `sessionImageOpenRequested(id)`.
- Filmstrip overrides by id (`ThumbnailBar::setSessionIds` + id overrides).
- **Image LoadReplace uses `clearLiveCanvas()`** — must **not** discard
  Workspace/Gallery stashes (was the root cause of “crop doesn’t stick”).
- Crop prior rect from **session id / item**, not path map alone.
- **Peer sync only when both have the same non-zero SessionImageId** (024).
- Workspace must not invent id from `m_currentSessionId` (024).
- Drop-duplicate: `placeOrMoveImageAt(path, pos, sid, slot)` binds id immediately.

### Build / packaging

- CMake: stricter warnings (`-Wall -Wextra -Wpedantic` + more); optional
  `QIMGVIEW_WERROR`.
- Warning cleanups (shadow, unused, double-promotion, QWidget::data shadow).
- Nix: `libsysprof-capture` in `buildInputs` to silence pkg-config noise from
  glib’s `Requires.private: sysprof-capture-4`.

---

## 3. Critical code map

| Concern | Where |
|---------|--------|
| Session list + ids | `MainWindow::m_files`, `m_sessionIds`, `allocSessionId`, `sessionIdAt` |
| Appearance by id | `ImageView::m_sessionAppearance` |
| Path last-writer cache (legacy) | `ImageView::m_itemStates` — **do not use as identity** |
| Edit commit + peer sync | `ImageView::commitItemSessionEdit` |
| Flip / 90° | `bakeItemFlip`, `bakeItemRotate90` |
| Crop enter prior | `prepareCropModeFullImage` |
| Crop record | `recordSessionCrop` |
| Live vs full wipe | `clearLiveCanvas` vs `clearWorkspace` |
| Stash | `stashWorkspaceItems` / `restoreStashedWorkspaceItems` |
| Duplicate UI | `MainWindow::duplicateSelected` + `ImageView::duplicateSelected` |
| Drop duplicate | `MainWindow::handleDroppedUrls` → `placeOrMoveImageAt(..., sid, slot)` |
| Rebind | `rebindWorkspaceSession(files, ids)` |
| Async load bind | `m_pendingSessionBinds` |

---

## 4. Correct end-to-end path (verify when continuing)

1. Workspace: open image A → one session image id₁, tile bound to id₁.
2. Duplicate (or drag-drop same path onto canvas) → id₂ + new tile bound to id₂.
3. Flip chrome on **tile₂ only** → only tile₂ pixels/content flags change.
4. Double-click tile₂ → Image mode with `currentSessionId = id₂`; stash keeps tiles.
5. Crop apply → `m_sessionAppearance[id₂]` + sync only peers with id₂ (stashed tile₂).
6. Return Workspace → tile₂ cropped; tile₁ unchanged.
7. Re-enter crop on tile₁ vs tile₂ → each shows **its own** prior rect.

If any step fails, check **sessionId on the live item** first (`-1`/`0` means
unbound and edits will not propagate correctly in Workspace).

---

## 5. Known residual issues / incomplete work

### High priority

1. **Unbound tiles (`sessionId == 0`)**  
   Still possible if something adds to the canvas without going through
   `addImageForSession` / `placeOrMoveImageAt(..., id)` / rebind. Workspace
   edits on unbound tiles **do not** write `m_sessionAppearance` and **do not**
   sync (by design after 024). Ensure every placement path assigns an id.

2. **Open-by-path still exists**  
   `galleryItemOpenRequested(path)`, `showPathInImageMode`, `indexOf(path)` resolve
   the **first** session row with that path. Prefer id or explicit index when
   duplicates exist. Gallery open should eventually pass session id.

3. **`m_itemStates[path]` still written**  
   Last-writer cache for legacy. Any code that **reads** path map for appearance
   on a **bound** id will mis-handle duplicates. Audit remaining readers.

4. **Filmstrip path-only override signal**  
   3-arg id signals exist; path-only emits may still update all rows with that
   path if anything connects the old overload.

5. **Session undo remove/restore**  
   `SessionRemoveCommand` stores path+index, not id. Restore allocates **new**
   ids — Workspace associations after undo may not match pre-remove ids.

6. **`setWorkspacePaths(paths)` path-only**  
   Gallery/session rebuild still path-keyed; path duplicates in Gallery are
   underspecified (DOMAIN: one object per session image — needs packing by id).

### Medium

7. **Group flip / multi-select**  
   `transformTargets()` flips **all selected** items. Intended for multi-select;
   not a bug if selection includes duplicates.

8. **Opacity / handle polish**  
   Largely done; re-check after large/small frame transitions and HiDPI.

9. **`-Wnull-dereference`**  
   May still appear from Qt `QPointer` inlines depending on GCC version; local
   `guard.data()` pattern used in thumbnail jobs.

### Low / docs

10. **IDENTITY.md §1+** still describes some pre-id implementation detail;
    §0 is the intended model. Prefer this SESSION.md for “what to do next”.
11. **AUDIT.md H2p / M27** partially addressed by ids; mark fixed when
    verified at runtime.
12. **TODO.md 0.1.0** still lists broad stabilize items — fold identity
    acceptance tests into that list.

---

## 6. Bundle index (this series)

| Bundle | Topic |
|--------|--------|
| 001–013 | Chrome, opacity, crop UI, group rotate, undo, zoom, early session-slot work |
| 014–015 | IDENTITY.md / DOMAIN.md mental model |
| 016–017 | `SessionImageId`, appearance map, rebind + pending binds |
| 018 | **Stash preserved** across Image LoadReplace |
| 019 | Crop prior by session id |
| 020–022 | Compiler warnings + fixes |
| 023 | Nix `libsysprof-capture` |
| 024 | **Strict id-only peer sync**; drop-duplicate binds id |

---

## 7. Suggested next session checklist

1. Runtime: duplicate → flip one → drop-duplicate → flip one → Image crop each.
2. Grep for remaining **identity hazards**:
   - `m_itemStates.constFind` / `findItemByPath` used for appearance or open
   - `indexOf(path)` for navigation into Image mode
   - `clearWorkspace()` on Image-mode load paths (should be `clearLiveCanvas`)
3. Gallery: open tile when path is duplicated — pass session id.
4. Session remove undo: store/restore `SessionImageId`.
5. Optional: stop writing path appearance entirely once readers are gone.
6. Mark AUDIT M16/M27 resolved after verification.

---


---

## Continuation handoff (controllers → project, bundles ~016–038)

**Tip ref:** `b1fde01` (session-id membership / no path-map flip leakage)  
**Stack:** upstream + `qimgview-016-…` through `qimgview-038-…` (see artifacts).

### What landed after the identity series

| Theme | Bundles (approx.) | Notes |
|-------|-------------------|--------|
| Mode controllers | 016–027 | `GalleryController`, `WorkspaceController`, `ImageController`; host API on ImageView; Phase 5 marked complete in REFACTOR.md |
| Duplicate tiles / holes | 028–030 | Multiplicity-aware remove + pack; LoadAdd creates all pathOrder occurrences; no pointer aliasing in reorder |
| Crash on history open | 029 | Collect-then-destroy; reorder dedupe by distinct item |
| Undo | 031–032 | Flip/rotate/raise/opacity/reset; Duplicate = session + canvas |
| Menus | 033 | Content ops + sort under Edit; View = display |
| Edge scale Ctrl | 034 | Mid-edge matches corner (default opposite edge) |
| Project + export | 035–037 | `.qimgview` JSON + SHA-256; Export PNG; fit page guide; relink |
| Membership / flip id | 038 | Thumbnail toggle by SessionImageId; bound tiles skip path map |

### Chrome visibility (bundle 039)

- Thumbnail strip preference is **per mode**: Workspace default on, Gallery default off
  (`thumbnailsPreferredWorkspace` / `thumbnailsPreferredGallery` in QSettings).
- Layout dock is Workspace-only: hidden by default, toggle disabled outside Workspace,
  preference `layoutPreferredInWorkspace` (default off). Gallery/Image never show it.
- Fullscreen leave re-applies mode helpers instead of a single pre-FS snapshot for
  thumbs/layout.

### Remaining residuals (updated)

1. **Runtime QA** still needed for duplicate × membership × flip × project round-trip.
2. Undo still missing for membership hide/show, sort, layout switch.
3. Path map `m_itemStates` still exists for unbound tiles; prefer id-only long term.
4. Relink does not rewrite the project file automatically after manual locate.
5. Fit page guide uses content AABB (not a print page size); printer setup clears content rect.

### Doc pointers

- Controllers / host API: [REFACTOR.md](REFACTOR.md) Phase 5 + post-Phase 5 log  
- Project format: [TODO.md](TODO.md) “Workspace project files”  
- User-facing export: [README.md](README.md) “Project files and export”  
- Domain persistence: [DOMAIN.md](DOMAIN.md)


## 8. Author / commit convention (this series)

- Author: `Ingo Ruhnke <grumbel@gmail.com>`
- Trailer: `Co-authored-by: Grok <grok@x.ai>`
- Bundles: `qimgview-NNN-short-name.bundle`, ref `HEAD`, stack cleanly.

---

## Continuation handoff (040–049 — grade, clipboard, background)

**Tip:** apply `qimgview-040` … `qimgview-049` in order onto the prior tip.

| Bundle | Topic |
|--------|--------|
| 040 | Colour grade survives Duplicate / thumbs / restore |
| 041 | Project load places only pose tiles (not full session) |
| 042 | `SessionAppearance::applyContentToItem` central content path |
| 043–044 | Workspace Copy/Cut/Paste + paste stack / selection polish |
| 045–047 | Per-project Workspace background + dialog + portable paths |
| 048 | Paste MIME gating, Edit menu, cut/paste undo, apply harden, bg SHA |
| 049 | Background undo, live preview, tile LOD, embed into `.assets/` |

### Model reminders

- Content appearance (crop, content flips/turns, colour grade) is keyed by **SessionImageId**.
- Workspace canvas membership is a **subset** of the session (pose present ⇒ on canvas).
- Workspace background **AppDefault** = Preferences; custom modes are project state.
- Background image tiles outside the project dir are copied to `<stem>.assets/` on save; unused `bg-*` files are pruned on later saves.

### Residual risks

- `applyContentToItem` still assumes full-source pixels for geometry ops (callers reload).
- Background assets are not referenced from the main `assets[]` array (side folder only).
- No compile verification in the agent sandbox (no Qt6 dev packages).

