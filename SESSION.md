# Session handoff — identity, chrome, crop (bundles 001–024)

**Tip ref (identity series):** `ae3043f` — see **Continuation handoff** below for later tip `b1fde01`  
**Apply order:** `biltoo-001-…` through `biltoo-024-…` from artifacts, each
`git pull <bundle> HEAD` in sequence. Base is upstream `Grumbel/biltoo` tip
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
  `BILTOO_WERROR`.
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

### High priority — largely closed (2104–2123)

Identity placement, open/focus/remove/reveal, reorder, and path→index resolution
prefer **SessionImageId → list index → path**. Remaining path use is intentional
last-resort for fully unbound rows (no id at all).

1. **Unbound tiles** — closed for normal create paths (2108–2109, 2116).  
   Workspace edits on unbound tiles still skip appearance sync (by design).  
   `PendingItemAppearanceBook` / `bindSelectedSessionIds` = collision recovery only.

2. **Open-by-path** — fallback only; see `emitItemOpenInImageMode`,
   `indexOfPathPreferId`, Gallery focus/remove/reveal (2112–2123).  
   Residual: pure `paths().indexOf` only when the session row has no id.

3. **Path map** — bound writes no-op (2110). Unbound tiles remain path-book clients.

4. **Filmstrip path-only override signals** — fixed (2104).

5. **Session undo remove/restore** — fixed (id-keyed snapshots).

6. **`setWorkspacePaths(paths)` path-only** — removed (2108); reorder prefers id (2113).

### PreferCache / thumtoo ladder (characterization, 2124)

Durable decode climbs a fixed edge ladder (`ThumtooCache::kLadderEdges`:
128…8192). Product hosts should prefer **display/overview** schedules over
legacy soft PreferCache encode:

| Band | Edge (approx.) | API / role |
|------|----------------|------------|
| Soft / filmstrip | ≤ soft max (~512) | SoftPreview; LQIP / soft ladder |
| Gallery soft max | `kGalleryLadderEdge` | SoftPreview until native coverage |
| Batch overview | above soft, ≤ batch | `scheduleOverviewPixels` |
| Display PreferCache | up to `kImageLadderEdge` (8192) | `scheduleDisplayPixels` → `ladderReady` |
| Native / full | covers logical size | `FullSource`; `request_full_pixels` |

Classification uses **delivered** long edge (`classifyImageModeSample`), not the
request edge. Soft samples stay SoftPreview so PreferCache/native can upgrade.
Tiles: after `durableTilesReady`, tile LOD owns display past soft max; PreferCache
TileSynth needs known durable tiles (`hasDurableTilesKnown`).

PreferCache host schedule rules folded into SIZE.md (biltoo-2125).
Optional follow-ups: runtime QA of ladder upgrade under Gallery scroll + Image focus.

### Medium

7. **Group flip / multi-select**  
   `transformTargets()` flips **all selected** items. Intended for multi-select;
   not a bug if selection includes duplicates.

8. **Opacity / handle polish**  
   Largely done; re-check after large/small frame transitions and HiDPI.

9. **`-Wnull-dereference`**  
   Qt `QPointer` inlines may still warn depending on GCC version. Host pattern:
   capture `QPointer`, check `if (!guard)` in worker and again in the GUI
   lambda before `guard->…` / `guard.data()` (displaypipeline_jobs, MainWindow
   search, rematerialize). Treat new warnings as pattern misses, not model bugs.

### Low / docs

10. **IDENTITY.md §1** updated for SessionDocument + SessionImageId
    (biltoo-2126); §0 remains the product model. Prefer SESSION.md for
    “what to do next”.
11. **AUDIT.md M16 / M27** marked fixed via SessionImageId (biltoo-2128);
    runtime smoke still welcome (duplicate → leave Workspace → return).
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
3. Gallery: open tile when path is duplicated — pass session id (double-click
   already prefers `sessionImageOpenRequested` / slot index).
4. Session remove undo by id — done (see §5).
5. Optional: stop writing path appearance entirely once readers are gone.
6. Mark AUDIT M16/M27 resolved after verification.

---


---

## Continuation handoff (controllers → project, bundles ~016–038)

**Tip ref:** `b1fde01` (session-id membership / no path-map flip leakage)  
**Stack:** upstream + `biltoo-016-…` through `biltoo-038-…` (see artifacts).

### What landed after the identity series

| Theme | Bundles (approx.) | Notes |
|-------|-------------------|--------|
| Mode controllers | 016–027 | `GalleryController`, `WorkspaceController`, `ImageController`; host API on ImageView; Phase 5 marked complete in REFACTOR.md |
| Duplicate tiles / holes | 028–030 | Multiplicity-aware remove + pack; LoadAdd creates all pathOrder occurrences; no pointer aliasing in reorder |
| Crash on history open | 029 | Collect-then-destroy; reorder dedupe by distinct item |
| Undo | 031–032 | Flip/rotate/raise/opacity/reset; Duplicate = session + canvas |
| Menus | 033 | Content ops + sort under Edit; View = display |
| Edge scale Ctrl | 034 | Mid-edge matches corner (default opposite edge) |
| Project + export | 035–037 | `.biltoo` JSON + SHA-256; Export PNG; fit page guide; relink |
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
- Bundles: `biltoo-NNN-short-name.bundle`, ref `HEAD`, stack cleanly.

---

## Continuation handoff (040–049 — grade, clipboard, background)

**Tip:** apply `biltoo-040` … `biltoo-049` in order onto the prior tip.

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
- Background image tiles are referenced only in the project JSON (path, optional pathRelative, imageSha256) — no side `.assets/` folder.

### Residual risks

- `applyContentToItem` still assumes full-source pixels for geometry ops (callers reload).
- Background image is path+checksum in `workspaceBackground` JSON (not copied).
- No compile verification in the agent sandbox (no Qt6 dev packages).

