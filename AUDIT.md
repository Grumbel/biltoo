# QImgView full audit

**Date:** 2026-08-30  
**Tree tip:** `1f264d1` (HUD index bold) + stacked bundles through `qimgview-015`  
**Scope:** All project sources, headers, build, packaging, DOMAIN/TODO/AGENTS, desktop entry. **No code changes in this pass** — findings only.

---

## Protocol

| Step | Activity | Status |
|------|----------|--------|
| P0 | Inventory tree, sizes, licenses, build entry points | [x] |
| P1 | Domain model vs implementation (`DOMAIN.md`) | [x] |
| P2 | Application entry (`main.cpp`, CLI) | [x] |
| P3 | Shell: `MainWindow` + session / gallery / UI modules | [x] |
| P4 | Canvas: `ImageView` (+ input, layout, types) | [x] |
| P5 | Objects: `ImageItem` + interaction chrome | [x] |
| P6 | Thumbnails: `ThumbnailBar` | [x] |
| P7 | Preferences, metadata, default-apps | [x] |
| P8 | Load path: `ImageLoader`, async races | [x] |
| P9 | Packaging: CMake, Nix, `.desktop`, REUSE | [x] |
| P10 | GNOME 2 HIG / accessibility / shortcuts | [x] |
| P11 | Docs consistency (TODO, DOMAIN, AGENTS, README) | [x] |
| P12 | Consolidate findings by severity | [x] |

**Method:** Read every `.cpp`/`.h` under `src/`, project markdown, `CMakeLists.txt`, `default.nix`/`flake.nix`, `data/qimgview.desktop`. No runtime/Qt build in this environment — crash/race notes are static analysis.

---

## Inventory (P0)

| Area | Notes |
|------|--------|
| Sources | ~11.2k lines C++ across 17 `.cpp` + 12 `.h` |
| Largest units | `imageview_input.cpp` (~1173), `imageitem_interaction.cpp` (~1118), `mainwindow.cpp` (~1116), `imageview.cpp` (~1090), `thumbnailbar.cpp` (~914) |
| License | GPL-3.0-or-later; SPDX headers on sources; REUSE + `.reuse/dep5` |
| Build | CMake + optional VIPS/GIO; Nix flake + `default.nix` |
| Resources | `src/icons.qrc`, `data/icons/`, `data/qimgview.desktop` |

---

## Findings by severity

### Critical / high — correctness, crashes, data loss risk

| ID | Area | Issue |
|----|------|--------|
| H1 | Domain vs code | **DOMAIN.md still says slideshow is “Image only”.** Implementation allows start from Gallery (`startSlideshow` → `showPathInImageMode`). TODO interaction table still says “Space \| Slideshow (Image only)”. Spec and product disagree. |
| H2 | Chrome dual path | **Handle hover/cursor still implemented on `ImageItem` (`hoverMoveEvent`) and again on `ImageView` (view-driven).** View path is authoritative for paint/hits; item path still mutates cursor, tooltip, `m_hoverHandle`, and calls `update()`. Risk of desync, double work, and “ghost” highlights when view and item disagree (especially under another pixmap). AGENTS.md already warns dual paths must stay consistent — they are not a single ownership model. |
| H3 | Async load | **`LoadResult` / generation checks exist for replace-style loads**, but rapid mode switches (Gallery ↔ Image ↔ Workspace) while thread-pool jobs complete can still apply a finished decode to the wrong mode or leave empty canvas until next navigation. Worth a dedicated race review with gen+mode token on every completion path (`loadImage`, `addImage`, gallery populate). |
| H4 | Undo | **Undo stack is cleared on several mode/canvas resets** (`clearExtras`, layout enters). Workspace transform undo is local to `ImageView`; session-level “remove files” is not on the same stack. User expectation of one Undo may be violated. |
| H5 | Gallery select + open | **Classic multi-select is implemented, but Enter opens “selectedGalleryItem” = first of `selectedItems()`**, not necessarily focus/anchor. Multi-select + Enter behaviour is underspecified. |

### Medium — design inconsistency, UX, maintainability

| ID | Area | Issue |
|----|------|--------|
| M1 | Raise/Lower | Overlap-based stacking is good, but **z values drift unboundedly** (`±1` from neighbour). Long sessions accumulate large z magnitudes; no renormalisation. |
| M2 | Image drop vs Gallery | Image mode plain drop **replaces** session; Gallery always **appends**. Documented in code/TODO but easy to mis-train users; no confirmation on replace. |
| M3 | FullViewportUpdate | Correct for overlay smear; **always full repaint** may cost on large galleries / high-DPI. Acceptable for now; no dirty-region strategy for HUD-only updates. |
| M4 | Gallery zoom | View transform zoom in Gallery **interacts with pack that assumes view pixels ≈ scene units** (`applyLayout` resets transform). User zoom can be wiped on resize/relayout — surprising. |
| M5 | `setWorldTransform` history | Chrome paint was rewritten to map via the view; **residual comments still mention drawForeground** in places (partially cleaned). Documentation lag. |
| M6 | Shortcut density | Many **ApplicationShortcut** actions (bindViewerShortcuts). Global shortcuts fire while preferences/file dialogs are open unless dialogs are application-modal and grab focus strongly — verify Escape/Space/H in dialogs. |
| M7 | Slideshow + Gallery enable | Slideshow enabled in Gallery, but **while slideshow runs, leaving Image for Gallery still does not auto-stop** only when `canSlideshow` is false (Workspace). User can enter Gallery via layout action during slideshow? Mode changes may leave timer running against Image expectations — check layout actions during active slideshow. |
| M8 | Thumbnail drag | Gallery ignores pure re-drops; **Workspace still accepts session paths as new canvas objects**. OK by design; ensure thumb multi-drag + modifiers stay predictable. |
| M9 | Metadata panel | Loads via Exiv2/path; **no obvious cancel when current index changes mid-read** (if async). Confirm synchronous vs async in `metadatapanel.cpp`. |
| M10 | Preferences HUD colours | **Alpha in stylesheet buttons is composited for display**; actual panel uses `m_hudPanelColor`. OK, but HexArgb round-trip must stay the only storage form (migration for old `#RRGGBBAA` exists). |
| M11 | `ImageItem::boundingRect` | Expanded for chrome pads while selected/interactive; **contentSceneRect** used for overlap. Hit tests and packing must not confuse the two — generally handled, still a footgun for new code. |
| M12 | Fit/fill vs item rotation | DOMAIN requires framing not to erase rotation. **Fit paths must be checked** that they only change view transform, not `itemRotation` (spot-check `fitItem`). |
| M13 | Status bar vs HUD | Parallel presentation of zoom/index/filename; **risk of different format strings** (badge `n/N` vs status text). |
| M14 | TODO stale rows | Interaction table still lists outdated slideshow/gallery click semantics relative to bundles 006–014. |

### Low — polish, HIG, style

| ID | Area | Issue |
|----|------|--------|
| L1 | GNOME 2 HIG — menus | Structure is broadly classic (File/Edit/View/Go/Help). **Workspace tools appear under View** and a separate toolbar — acceptable; ensure accelerators unique (Ctrl+R rotate vs other). |
| L2 | GNOME 2 HIG — buttons | About box uses single Close — good. Preferences OK/Cancel assumed via `QDialog` defaults — confirm button box order (platform-aware). |
| L3 | GNOME 2 HIG — feedback | Destructive session replace on drop has **no confirmation**. Delete from session/thumbs should confirm or be undoable (session remove stops slideshow but undo?). |
| L4 | Accessibility | `accessibleName`/`Description` on image view and thumbs — good start. **Chrome handles are painted, not accessible widgets** — no AT-API roles for scale/rotate. Keyboard path for raise/lower exists (shortcuts); handle-level keyboard missing. |
| L5 | Focus | Gallery spatial arrows and Image edge zones — **focus must remain on ImageView** after mode change; verify after `showPathInImageMode`. |
| L6 | Desktop file | `qimgview.desktop` present; MIME list must stay in sync with `DefaultApps::supportedMimeTypes()` (comment says so). **Suffix list in ImageLoader is wider than desktop MIME** (psd, exr, …) — files openable in-app but not registered as handler. |
| L7 | Icons | Theme icons with style fallbacks in `icons.cpp` — good. Embedded SVG fallback for app icon. |
| L8 | i18n | User-visible strings generally wrapped in `tr()`. **CLI help strings in `main.cpp` are not translated** (QStringLiteral). |
| L9 | Error paths | Failed decode: HUD/error string path exists (`m_lastLoadError`). **User-facing dialog on unreadable file** may be weak (status only). |
| L10 | Code size | Very large `.cpp` files (1k+ lines) hurt reviewability; further splits (HUD paint, chrome paint already partial) would help. |
| L11 | Magic numbers | Handle sizes, edge zone px, HUD pad duplicated as constexpr in places — mostly centralized in interaction file; edge zone widths in input. |
| L12 | `QGraphicsView` scroll | Image mode can disable scrollbars; zoomed image pan via drag — OK. **Key scroll fallback** must not steal Left/Right (handled). |
| L13 | Rubber-band Gallery | Enabled in Gallery; **Alt+pan still available** — verify rubber-band does not start on Alt-chords. |
| L14 | LICENSE file | Full GPLv3 text; REUSE points at dep5 — OK. |

### Informational / positive

| ID | Note |
|----|------|
| P+1 | Clear mode enum as source of truth (`ViewMode`) — domain-aligned. |
| P+2 | Generation token on async loads — correct pattern. |
| P+3 | FullViewportUpdate + viewport chrome — correct fix for overlay smear. |
| P+4 | ApplicationShortcut binding for fullscreen — correct response to hidden menus. |
| P+5 | Overlap-based raise/lower — better than blind z±1. |
| P+6 | REUSE/SPDX discipline. |
| P+7 | Nix separate debug info — good for field crashes. |

---

## File checklist

| File | Reviewed | Notes / issues |
|------|----------|----------------|
| `src/main.cpp` | [x] | CLI untranslated; ImageLoader::init before QApp — OK for VIPS |
| `src/mainwindow.h` | [x] | Large surface area; mode helpers clear |
| `src/mainwindow.cpp` | [x] | Drop policy matrix; settings; session badge pulse flag |
| `src/mainwindow_session.cpp` | [x] | Slideshow/Gallery enable; nav actions |
| `src/mainwindow_gallery.cpp` | [x] | Return-to-gallery; layout entry |
| `src/mainwindow_ui.cpp` | [x] | Actions/menus/toolbars; shortcut set |
| `src/mainwindow_includes.h` | [x] | Include hub — OK |
| `src/imageview.h` | [x] | Many responsibilities (view+controller) |
| `src/imageview.cpp` | [x] | Load, fit, session position, HUD setters |
| `src/imageview_input.cpp` | [x] | Paint HUD/chrome, mouse, keys, edges |
| `src/imageview_layout.cpp` | [x] | Gallery pack; mode enter/leave |
| `src/imageview_types.h` | [x] | Shared enums/structs |
| `src/imageitem.h` / `.cpp` | [x] | Transform state; contentSceneRect |
| `src/imageitem_interaction.cpp` | [x] | Chrome paint/hit; dual hover path (H2) |
| `src/gallerylayout.cpp` / `.h` | [x] | Pure packing — OK |
| `src/thumbnailbar.cpp` / `.h` | [x] | Multi-select, drag, decode pool |
| `src/preferencesdialog.cpp` / `.h` | [x] | HUD prefs; background; MIME defaults |
| `src/metadatapanel.cpp` / `.h` | [x] | Side panel |
| `src/imageloader.cpp` / `.h` | [x] | Qt then VIPS; suffix list |
| `src/defaultapps.cpp` / `.h` | [x] | GIO; MIME sync comment |
| `src/icons.cpp` / `.h` | [x] | Theme fallbacks |
| `CMakeLists.txt` | [x] | Qt6, optional VIPS/GIO |
| `default.nix` / `flake.nix` | [x] | RelWithDebInfo + separateDebugInfo |
| `data/qimgview.desktop` | [x] | MIME vs loader gap (L6) |
| `DOMAIN.md` | [x] | Slideshow/Gallery drift (H1) |
| `TODO.md` | [x] | Stale interaction rows (M14) |
| `AGENTS.md` | [x] | Chrome rules — still valid |
| `README.md` / `REUSE.md` / `LICENSE` | [x] | OK |

---

## GNOME 2 HIG lens (P10)

| Topic | Assessment |
|-------|------------|
| Menu layout | Classic File / Edit / View / Go-style actions present; not strictly HIG-named “Go” in all builds — check final menu titles |
| Keyboard | Shortcuts documented in About; ApplicationShortcut helps fullscreen |
| Direct manipulation | Workspace handles are visual; not HIG controls (no standard button metrics) |
| Dialogs | Preferences tabbed; colour buttons OK |
| Feedback | HUD flashes for some actions; silent slideshow advance (by request) |
| Consistency | Mode-specific enablement is complex — disabled actions need status tips explaining *why* (often missing) |
| Save confirmation | N/A (viewer); session replace on drop is the closest to data loss |

---

## Suggested fix order (for a later pass — not done now)

1. **Docs:** Align DOMAIN + TODO with Gallery slideshow and classic gallery select.  
2. **H2:** Single ownership of handle hover/cursor (view only; strip item hover chrome).  
3. **Load races:** Mode+generation token on all async completions.  
4. **Gallery zoom vs pack:** Define whether zoom survives relayout; implement deliberately.  
5. **HIG:** Status tips on disabled actions; optional confirm on session-replacing drop.  
6. **MIME vs suffixes:** Document or extend desktop MIME list.  
7. **Undo story:** Document or unify session vs canvas undo.

---

## Sign-off

Full static audit of the tree as of tip above. No behavioural code was modified in this pass. Next engineering work should pick severity order from the tables, not ad-hoc drive-by fixes.

---

## Deep pass (continuation)

**Date:** 2026-08-30 (same tip + this amendment)  
**Method:** Line-level follow-up on H3 load pipeline, fit/rotation (M12), gallery zoom (M4), dual chrome (H2), metadata, mode transitions, shortcut matrix.

| Step | Activity | Status |
|------|----------|--------|
| D1 | `scheduleImageLoad` / `onImageLoaded` all roles | [x] |
| D2 | Image-mode rotation/flip lifetime across navigation | [x] |
| D3 | `fitItem` / `zoomReset` vs DOMAIN framing rules | [x] |
| D4 | Gallery wheel zoom vs `zoomViewBy` / relayout | [x] |
| D5 | Slideshow vs `enterGalleryMode` / layout actions | [x] |
| D6 | Metadata panel load model | [x] |
| D7 | `ImageItem` mouse/hover vs view-owned chrome | [x] |
| D8 | `enterGalleryMode` double apply | [x] |
| D9 | Session navigation wrap, slideshow interaction | [x] |
| D10 | Shortcut collisions (spot check) | [x] |

---

### D1 — Async load generation model

**How it works**

- Every `scheduleImageLoad` does `++m_loadGeneration` and captures `gen` in the worker.
- `LoadReplace` completion **requires** `generation == m_loadGeneration` (latest wins).
- `LoadAdd` / `LoadRestore` do **not** compare generation; they gate on `m_pendingWorkspacePaths` and `findItemByPath`.

**Findings**

| ID | Severity | Issue |
|----|----------|--------|
| H3a | High | **Shared generation counter for all roles.** A `LoadAdd`/`LoadRestore` scheduled while a `LoadReplace` is in flight bumps `m_loadGeneration` and **drops the Image-mode navigation result**. Example: rapid Next, then something schedules an add (or gallery restore path), replace decode is discarded → blank or stuck previous frame until another navigate. |
| H3b | Medium | **No mode token on completion.** `LoadReplace` checks `isImageMode()` at completion time. If user switched to Gallery/Workspace after scheduling replace, the branch may no-op or seed workspace empty canvas depending on conditions — easy to reason wrong under stress. |
| H3c | Medium | **Gallery populate** goes through thumb selection → `syncCanvasFromThumbnailSelection` → many adds; each add bumps generation and can cancel an in-flight classic load if modes interleave. |
| H3d | Low | Null image on replace sets `m_lastLoadError` but does not clear a previous successful pixmap if clearWorkspace was not reached — actually clear happens only on success path; failed load keeps old image (good) but status may show error for path that is not displayed. |

**Recommendation (later):** Separate counters or roles: replace-gen vs add-set; pass `ViewMode` expected at schedule time; ignore completions when mode mismatch.

---

### D2 — Image-mode rotation/flip lifetime (**DOMAIN violation**)

In `onImageLoaded` for `LoadReplace` + Image mode:

```text
item->setItemScale(1.0);
item->setItemRotation(0.0);
item->setPos(0, 0);
```

Every successful navigation/open builds a **new** `ImageItem` with **rotation forced to 0**. Flips live on the item; a new item starts unflipped unless applied from saved state (not done here).

DOMAIN.md: *“The image may still carry rotation and flips; framing must not erase them unless the user asks to reset.”*

| ID | Severity | Issue |
|----|----------|--------|
| H6 | High | **Rotate/flip in Image mode is lost on Next/Prev/reload.** User rotates, steps once, orientation is gone. Not a view-only framing bug — object orientation is wiped at decode apply. |
| M12 ✓ | Confirmed OK for fit | `fitItem` comments and body only normalize **scale** and view matrix; they do **not** clear rotation. The wipe is solely in `onImageLoaded` replace path. |

**Recommendation:** Persist per-path orientation in session or `m_itemStates`, reapply after decode; only clear on explicit Reset.

---

### D3 — Zoom 1:1 in Image mode

`zoomReset()` in Image mode calls `item->setItemScale(1.0)` and `resetTransform()` — correct for 1:1 **pixels**, leaves rotation intact. Consistent with fitItem’s scale normalization. No new issue beyond H6 when combined with reload.

---

### D4 — Gallery zoom inconsistency (deepens M4)

| Path | Gallery behaviour |
|------|-------------------|
| `zoomViewBy` / toolbar Zoom In/Out | **Early return** — no view zoom |
| `wheelEvent` Ctrl/Meta or no-overflow plain wheel | **Scales the view transform** |
| `applyLayout` / resize debounce | **`resetTransform()`** — zoom discarded |

| ID | Severity | Issue |
|----|----------|--------|
| M4a | Medium | Toolbar zoom disabled in Gallery while wheel zoom works — **inconsistent affordances**. |
| M4b | Medium | Any relayout/resize **silently resets** wheel zoom — feels like a bug if user zoomed to inspect. |
| M4c | Low | Status/`viewScale()` may report zoom until next pack; after pack, identity without HUD explanation. |

---

### D5 — Slideshow vs Gallery entry

- `updateNavigationActions`: stops slideshow only when `!canSlideshow` (Workspace or ≤1 file).
- `enterGalleryMode` does **not** call `stopSlideshow()`.

| ID | Severity | Issue |
|----|----------|--------|
| M7a | Medium | **Start slideshow (Image), then activate a Gallery layout action:** timer keeps running; `onSlideshowTick` → `goNext` → `setCurrentIndex` only `focusSessionPath` in Gallery (does not show full-screen next image). Slideshow appears “running” (action checked, cursor hide) while user browses tiles — **mode/timer mismatch**. |
| M7b | Low | Entering Gallery from slideshow does not flash stop; UI still shows Stop Slideshow. |

**Recommendation:** `stopSlideshow()` at start of `enterGalleryMode` / Workspace enter; or advance only when `isImageMode()`.

---

### D6 — Metadata panel

- `setImagePath` runs **synchronously** on the GUI thread (Exiv2 read in-panel).
- No async cancel issue (M9 largely **cleared** for race-on-completion).
- Large files / slow FS can **stall UI** when stepping quickly through a session with the dock visible.

| ID | Severity | Issue |
|----|----------|--------|
| M9 → L15 | Low | Sync Exiv2 on GUI thread — jank risk, not a stale-result race. |

---

### D7 — Dual chrome path (deepens H2)

| Path | Still active? |
|------|----------------|
| `ImageView` mouse press/move — handleAt, drag, hover setHoverHandle, viewport cursor | Yes — primary |
| `ImageItem::mousePressEvent` → `beginHandleInteraction` | Yes — “fallback” |
| `ImageItem::hoverMoveEvent` | Yes — sets `m_hoverHandle`, tooltips, **item** cursor, `update()` |
| `ImageItem::paint` | Selection frame only; chrome not drawn here |

| ID | Severity | Issue |
|----|----------|--------|
| H2a | High | Two drag pipelines can both accept the same gesture depending on scene delivery order — **double begin** or fight with view’s `m_handleDragItem`. |
| H2b | Medium | Item hover still drives cursor on the **item** while view sets **viewport** cursor — competing cursors. |
| H2c | Low | Gallery branch sets `m_galleryHovered` and `update()` though paint no longer uses hover wash — **dead state + extra repaints**. |

---

### D8 — `enterGalleryMode` double pack

```text
m_imageView->enterGallery(layout);
populateGalleryCanvas();
m_imageView->enterGallery(layout);  // second time
```

| ID | Severity | Issue |
|----|----------|--------|
| M15 | Medium | **Double enterGallery** forces two full packs and duplicate work; second call papered over async item arrival. Fragile; should be single enter after items settle (or pack-on-item-added). |

---

### D9 — Navigation / slideshow

- `goNext`/`goPrevious` wrap the session (circular) — fine for slideshow.
- User Next while slideshow active **stops** slideshow (`!m_slideshowAdvancing`) — good.
- Timer tick sets `m_slideshowAdvancing` so it does not self-stop — good.
- Combined with M7a, ticks in Gallery only move session cursor — weak.

---

### D10 — Shortcuts (spot check)

| Combo | Binding | Notes |
|-------|---------|--------|
| Space | Slideshow ApplicationShortcut | Gallery now enabled — OK |
| H | HUD | vs Ctrl+H flip — distinct |
| R / Ctrl+R / ] | Rotate right | Bare **R** rotates — can surprise in text fields if any; app has few text fields |
| Ctrl+F | Zoom fill | Not find — OK for viewer |
| F / F11 | Dedicated QShortcuts + menu action without shortcut | Good leave-fullscreen story |
| Ctrl+Shift+Up/Down | Raise/Lower | Overlap logic — OK |

No hard duplicate key conflicts found in the action table beyond intentional multi-key rotate.

Preferences dialog: standard `QDialog` + buttons; ApplicationShortcuts may still fire **H**/**Space** while dialog focused depending on Qt focus — residual M6.

---

### D11 — Additional notes from deep read

| ID | Severity | Issue |
|----|----------|--------|
| M16 | Medium | **`findItemByPath` prevents second canvas object with same path.** `duplicateSelected` must use a distinct identity or path key — if duplicates share path, restore/add skips. Verify duplicate implementation allows two items same path. |
| L16 | Low | Gallery item hover cursor is PointingHand though primary action is select not open — mild HIG mismatch (hand usually means open/navigate). |
| L17 | Low | `zoomViewBy` comment still says packaged layouts skip zoom; wheel path contradicts — comment debt. |

---

## Updated priority list (after deep pass)

1. **H6** — Persist Image-mode rotation/flip across navigation (DOMAIN).  
2. **H3a** — Split generation / don’t let LoadAdd cancel LoadReplace.  
3. **H2 / H2a** — Single chrome interaction owner (view only).  
4. **M7a** — Stop slideshow when leaving Image mode.  
5. **M4a/b** — One Gallery zoom policy (toolbar + wheel + relayout).  
6. **M15** — Single `enterGallery` after canvas populated.  
7. Docs H1/M14 alignment.  

---

## Sign-off (deep pass)

Static deep pass complete for load pipeline, orientation lifetime, gallery zoom, slideshow/mode, metadata, chrome dual path, gallery enter. Still no runtime execution in this environment. No product code modified in this amendment.

---

## Deep pass 2 (continuation)

**Focus:** status/HUD pulse coupling, path identity, loader EXIF (Qt vs VIPS), thumbnails, fullscreen chrome, defaults/MIME, packaging.

| Step | Activity | Status |
|------|----------|--------|
| E1 | `statusChanged` → `updateStatus` → `setSessionPosition` pulse | [x] |
| E2 | Session path string identity (canonical paths) | [x] |
| E3 | Qt `AutoTransform` vs VIPS orientation | [x] |
| E4 | ThumbnailBar selection/drag/remove | [x] |
| E5 | Fullscreen UI chrome restore | [x] |
| E6 | Default-apps / desktop MIME alignment | [x] |
| E7 | CMake / Nix packaging notes | [x] |
| E8 | Status bar comment debt / colour readout | [x] |

---

### E1 — Identity pulse fires on almost every status update (**severe UX**)

Call chain:

```text
ImageView::statusChanged
  → MainWindow::updateStatus
    → setSessionPosition(index, total, !m_slideshowAdvancing)
```

`setSessionPosition` pulse condition:

```text
if (pulseIdentity && (changed || total > 0)) {
    m_hudIdentityPulse = true;
    m_hudFlashTimer->start(1000);
}
```

When a session exists (`total > 0`), **`changed` is irrelevant** — any `updateStatus` with `pulseIdentity == true` restarts the identity HUD for 1s.

`statusChanged` is emitted from zoom, layout, handle end, mode changes, loads, etc.

| ID | Severity | Issue |
|----|----------|--------|
| **H7** | **High** | **Pinned-off HUD still pops filename/index on zoom, pan end, workspace edits, etc.** Contradicts “only user nav or H”. Slideshow quiet path only suppresses when `m_slideshowAdvancing`; normal interaction does not. Root bug is `(changed \|\| total > 0)` — should pulse only when `changed` (and pulseIdentity), or call sites should pass `pulseIdentity=false` except navigation. |
| H7b | Medium | Restarting the timer on every status event **extends** the overlay while the user works — sticky HUD without H. |
| L18 | Low | Comment in `updateStatus` still says share `[n/N]`; badge format is already `n/N`. |

---

### E2 — Path identity

- Session membership uses raw `QString` from dialogs/CLI/`QUrl::toLocalFile()`.
- Dedup is `QSet<QString>` / `m_files.contains(p)` — **no `QFileInfo::canonicalFilePath()`**.
- Same inode via `./a.jpg` and `/abs/a.jpg`, or symlink vs target, can appear **twice**.
- Gallery “novel only” drop uses the same string equality.

| ID | Severity | Issue |
|----|----------|--------|
| M17 | Medium | **Non-canonical paths** → duplicate session entries, duplicate thumbs, confusing Next/Prev. |
| M17b | Low | `findItemByPath` string match — workspace may hold one spelling while session holds another. |

---

### E3 — Orientation: Qt vs VIPS

- Qt path: `QImageReader::setAutoTransform(true)` — applies EXIF orientation on load. **Good.**
- VIPS fallback path: colourspace/cast/bandjoin — **no `vips_autorot` (or equivalent) observed** in `imageloader.cpp`.

| ID | Severity | Issue |
|----|----------|--------|
| M18 | Medium | Images that **fail Qt and succeed VIPS** may display with **wrong camera orientation** relative to Qt-loaded files. Inconsistent thumb vs full if one path uses Qt scaled and full uses VIPS (thumbs use same loader order — both try Qt first). |

Combined with **H6** (user rotation wiped on navigate): camera orientation is applied at load; user rotation is not persisted — two different orientation layers, only one sticky per decode.

---

### E4 — ThumbnailBar

| Topic | Assessment |
|-------|------------|
| Layout math | Documented cell size / label band — careful |
| Async thumbs | Generation cancel on `setFiles` — good pattern |
| Multi-select | Shift range, Ctrl toggle — aligned with Gallery classic select spirit |
| Drag | Image mode file drag; Workspace Alt+drag for files — easy to miss (discoverability) |
| Press item lifetime | Guards `row(m_pressItem) < 0` after model rebuild — good |
| Labels | Optional hide — OK |

| ID | Severity | Issue |
|----|----------|--------|
| M19 | Medium | Workspace thumb drag **without Alt** does not drag files and does not rubber-band (event accepted, no-op) — **dead gesture**. |
| L19 | Low | Thumb context menu / Delete vs session remove — confirm undo story (session remove not on QUndoStack). |

---

### E5 — Fullscreen UI

- `updateFullscreenUi` hides menu/toolbars/status; restores from “before fullscreen” flags.
- Slideshow may force fullscreen; leaving FS stops slideshow.

| ID | Severity | Issue |
|----|----------|--------|
| L20 | Low | Nested toolbars (workspace bar) visibility coupled to mode and FS — generally handled via `updateWorkspaceActionVisibility`. |
| M6 residual | Medium | ApplicationShortcuts remain the right fix for FS; dialog focus still worth manual test. |

---

### E6 — MIME / defaults

- `DefaultApps::supportedMimeTypes()` and `qimgview.desktop` kept in sync by comment discipline.
- `ImageLoader::imageSuffixes()` is **strictly wider** (psd, exr, fits, …).

| ID | Severity | Issue |
|----|----------|--------|
| L6 restated | Low | Open-with registration does not cover all loadable suffixes — expected unless desktop MIME expanded. |
| L21 | Low | GIO default-app changes need a session/running app refresh story — tree refresh on dialog open only. |

---

### E7 — Packaging

| Topic | Notes |
|-------|--------|
| CMake | Optional VIPS/GIO/Exiv2; defines `QIMGVIEW_HAVE_*` |
| Nix | RelWithDebInfo + `separateDebugInfo` — good for field debug |
| Version | `VERSION` + flake dirty/rev composition |
| Desktop | `StartupWMClass` / icon name should match binary — verify against installed name `qimgview` |

No packaging blocker found in static read; install path of `qimgview.desktop` and icon sizes not fully traced file-by-file in this pass.

---

### E8 — Status bar colour readout

- Mouse RGB + swatch on `mouseInfoChanged` — solid.
- No alpha in readout for images with alpha (shows RGB only) — minor.

| ID | Severity | Issue |
|----|----------|--------|
| L22 | Low | Alpha channel not shown in status colour line. |

---

## Revised top priorities (all passes)

1. **H7** — Pulse only on real session index change (or explicit nav), not on every `statusChanged`.  
2. **H6** — Persist Image-mode user rotation/flip across loads.  
3. **H3a** — Split load generation by role.  
4. **H2** — Single chrome interaction owner.  
5. **M7a** — Stop slideshow when leaving Image mode.  
6. **M4** — Unified Gallery zoom policy.  
7. **M17** — Canonicalize session paths.  
8. **M18** — VIPS autorot (or document Qt-only orientation).  
9. Docs H1/M14.  

---

## Sign-off (deep pass 2)

Additional static pass complete. Highest new defect: **H7 identity pulse condition** making the HUD appear continuously during normal interaction despite H-off and slideshow-quiet work. No product code modified.

---

## Deep pass 3 (continuation)

**Focus:** lifetime/UAF, undo safety, shortcut collisions, Delete semantics, edge zones, CLI.

| Step | Activity | Status |
|------|----------|--------|
| F1 | `clearWorkspace` / item delete vs live pointers | [x] |
| F2 | `QUndoStack` vs deleted `ImageItem*` in commands | [x] |
| F3 | Zoom Out vs Opacity Down shortcuts | [x] |
| F4 | Delete/Backspace workspace vs session | [x] |
| F5 | Edge zone geometry | [x] |
| F6 | CLI options completeness | [x] |
| F7 | `setWorkspacePaths` partial cleanup | [x] |

---

### F1 / F7 — Dangling interaction pointers

`clearWorkspace()` nulls `m_rotateItem`, `m_dragItem`, clears `m_items`, then `m_scene->clear()` (deletes items). It does **not** null:

- `m_handleDragItem`
- `m_gallerySelectionAnchor` (usually non-Gallery when clearing, still stale)

`setWorkspacePaths` when removing items nulls `m_dragItem` / `m_rotateItem` if matched, but **not** `m_handleDragItem`.

| ID | Severity | Issue |
|----|----------|--------|
| **H8** | **High** | **Use-after-free risk:** delete the item under active handle drag (mode switch, thumb sync removing path, clear workspace) while `m_handleDragItem` still non-null → next move/release touches freed memory. |
| H8b | Medium | `m_gallerySelectionAnchor` can point at destroyed items if canvas rebuilt without clearing anchor (Gallery leave clears it; other rebuild paths may not). |

---

### F2 — Undo commands hold raw `ImageItem*`

`TransformCommand` captures `ImageItem *` for undo/redo.

| ID | Severity | Issue |
|----|----------|--------|
| **H9** | **High** | **`restoreWorkspace()` → `clearWorkspace()` does not `m_undoStack->clear()`.** Prior transform commands still reference deleted items. User Undo after workspace restore → crash or corruption. |
| H9b | Medium | `prepareImageModeCanvas` / `prepareGalleryCanvas` / `clearExtras` do clear the stack — inconsistent policy. Any new clear path that forgets clear inherits H9. |
| H9c | Low | Commands are not listed in a custom text beyond default; acceptable. |

---

### F3 — Shortcut collision

| Action | Shortcut |
|--------|----------|
| Zoom Out | `QKeySequence::ZoomOut` → typically **Ctrl+−** |
| Opacity Down | **Ctrl+Key_Minus** (explicit) |

Both become `ApplicationShortcut` via `bindViewerShortcuts()`.

| ID | Severity | Issue |
|----|----------|--------|
| **M20** | Medium | **Ctrl+− bound to two actions.** Qt may run both or an arbitrary one; Workspace opacity and view zoom fight. Zoom In (`Ctrl++`) vs Opacity Up (`Ctrl+=`) related ambiguity on some layouts. |

---

### F4 — Delete key semantics

- Workspace: Delete/Backspace removes **canvas** items, session list unchanged, state remembered — matches DOMAIN “remove from canvas”.
- Not undoable via QUndoStack (only remember/re-add via thumb).
- Gallery/Image: Delete does not remove from session in this handler (falls through) — session remove is thumb/context elsewhere.

| ID | Severity | Issue |
|----|----------|--------|
| L23 | Low | No confirmation on canvas Delete; acceptable if documented. |
| M21 | Medium | Users may expect Delete in Image mode to remove file from session — it does not (HIG expectation gap). |

---

### F5 — Edge zones

- Left/right strips for prev/next; top for Gallery return when available.
- Priority: top checked before left/right in `edgeZoneAt` — good.
- Corners: top wins over left/right — OK.
- Widths are fixed viewport pixels — scale-invariant; OK.

No new defect beyond existing fullscreen hit testing already improved in earlier bundles.

---

### F6 — CLI

- fullscreen, fit (declared), start-at, recursive, sort, slideshow, interval, thumbnails toggles — present in `main.cpp`.
- `fitOption` registered; confirm it is applied on load (spot-check: may be no-op if default is already fit).

| ID | Severity | Issue |
|----|----------|--------|
| L24 | Low | Verify `--fit` is not a dead option (default fit mode already true). |

---

### F7 — Memory / ownership summary

| Pattern | Assessment |
|---------|------------|
| Scene owns items; explicit delete after removeItem | Consistent when both happen |
| `m_items` cleared before `scene->clear()` | Avoids double-free of list pointers |
| Thread pool loads use QueuedConnection to view | View must outlive jobs; generation helps replace |
| No cancel of QThreadPool on destroy | Destroy ImageView while jobs pending → **invokeMethod on destroyed QObject** (Qt drops if object gone? Queued to deleted object is safe if object destroyed — events discarded). Relatively OK |

| ID | Severity | Issue |
|----|----------|--------|
| M22 | Medium | Destroying window mid-decode: completions no-ops if generation/object gone; pending paths not a leak of ImageItem (not created). Acceptable. |

---

## Revised top priorities (passes 1–3)

1. **H7** — Identity pulse only on index change  
2. **H8 / H9** — Null all live item pointers on delete; always clear undo when items destroyed  
3. **H6** — Persist Image-mode orientation  
4. **H3a** — Split load generation by role  
5. **H2** — Single chrome owner  
6. **M20** — Disambiguate Ctrl+− zoom vs opacity  
7. **M7a** — Stop slideshow leaving Image  
8. **M4** — Gallery zoom policy  
9. **M17 / M18** — Paths / VIPS orient  
10. Docs  

---

## Sign-off (deep pass 3)

Lifetime/UAF and undo safety reviewed. Highest new issues: **H8** dangling `m_handleDragItem`, **H9** undo after `restoreWorkspace`/`clearWorkspace` without stack clear, **M20** Ctrl+− double binding. No product code modified.

---

## Deep pass 4 (continuation)

**Focus:** project infrastructure, HIG menu completeness, i18n, install/AppStream, tests, icons, error UX, Escape handling.

| Step | Activity | Status |
|------|----------|--------|
| G1 | CMake install + desktop/icon | [x] |
| G2 | AppStream / metainfo | [x] |
| G3 | Automated tests | [x] |
| G4 | i18n / translators | [x] |
| G5 | Menu bar HIG (File…Help) | [x] |
| G6 | Icons (theme + embedded + hicolor) | [x] |
| G7 | User-visible errors (failed open/decode) | [x] |
| G8 | Escape dual handlers | [x] |
| G9 | Opacity clamp / chrome solid | [x] |
| G10 | Settings persistence on close | [x] |

---

### G1 — Install layout

CMake installs:

- binary → `bin`
- `qimgview.desktop` → `applications`
- scalable app SVG → `icons/hicolor/scalable/apps`

| ID | Severity | Issue |
|----|----------|--------|
| L25 | Low | **No fixed-size PNG/hicolor icons** (16/32/48) — fine on many desktops with SVG support; some older panels prefer PNG. |
| L26 | Low | Action SVGs live in qrc only — not installed to hicolor; theme fallbacks used when theme has standard names — OK. |

---

### G2 — AppStream

| ID | Severity | Issue |
|----|----------|--------|
| M23 | Medium | **No AppStream metainfo (`.metainfo.xml` / `.appdata.xml`).** GNOME Software / Flathub / `appstreamcli` validation cannot describe the app; packaging for app stores needs this. |

---

### G3 — Tests

| ID | Severity | Issue |
|----|----------|--------|
| M24 | Medium | **No unit/integration tests** in CMake or tree (no gtest/QtTest targets). Regressions in pack math, path dedup, load generation, raise/lower overlap rely entirely on manual runs. `nix flake check` only builds. |

---

### G4 — i18n

| ID | Severity | Issue |
|----|----------|--------|
| L8 restated | Low | UI strings use `tr()` widely; **CLI help is `QStringLiteral` only**. |
| M25 | Medium | **No `QTranslator` / `.ts` / `.qm` pipeline** in CMake — the app is not localization-ready beyond English source strings. No lupdate target. |

---

### G5 — GNOME 2 HIG menus

Present: File, Edit (incl. Preferences), View, Go, Help (About only).

| ID | Severity | Issue |
|----|----------|--------|
| L27 | Low | Help has **About only** — no “Keyboard Shortcuts” dialog (common HIG pattern; About lists a few). |
| L28 | Low | Preferences under **Edit** is acceptable (GNOME-style); some apps use Edit or separate — OK. |
| L29 | Low | Context menu is long (file, nav, zoom, rotate, workspace) — still usable; not mode-filtered (disabled actions remain visible). **HIG:** hide or disable with explanation; disable is used via global action state — OK if enablement is correct. |

---

### G6 — Icons

- Rich set of action SVGs in `data/icons/actions` + qrc.
- `themeIcon()` with style pixmap fallbacks.
- App icon theme name `qimgview` + embedded SVG.

| ID | Severity | Issue |
|----|----------|--------|
| L30 | Low | Flip actions may lack dedicated flip icons (rotate icons used elsewhere) — cosmetic. |

---

### G7 — Error UX

Failed decode sets `m_lastLoadError` and status/HUD filename path — **no modal error** for unreadable file on open.

| ID | Severity | Issue |
|----|----------|--------|
| M26 | Medium | **Silent failure** when a path cannot be decoded (especially multi-file open where some fail) — user may not notice missing images. HIG prefers status + optional dialog for explicit Open. |

---

### G8 — Escape handling

- `QShortcut(Escape, ApplicationShortcut)`: leave fullscreen, else return to gallery if active.
- `MainWindow::keyPressEvent` also handles Escape for fullscreen.

| ID | Severity | Issue |
|----|----------|--------|
| L31 | Low | **Dual Escape paths** — redundant but consistent; ensure return-to-gallery does not fight dialog Escape (dialog should consume key first). |

---

### G9 — Opacity

`setItemOpacity` clamps to **[0.05, 1.0]**; item `QGraphicsItem::opacity` forced to 1 so chrome stays solid. Sound design.

No defect.

---

### G10 — Settings on close

`closeEvent` → `writeSettings()` — good. Preferences OK also writes. Crash mid-session can lose last settings — normal.

---

### G11 — Coverage map (what the audit has touched)

| Area | Passes |
|------|--------|
| Domain / docs drift | P1, H1, M14 |
| Load races / generation | D1, H3a–d |
| Orientation lifetime | D2, H6, M18 |
| Chrome dual path / UAF | H2, H8, F1 |
| Undo safety | H9 |
| HUD pulse | H7 |
| Gallery zoom / enter | M4, M15 |
| Slideshow / modes | M7, H1 |
| Paths / MIME | M17, L6 |
| Thumbs | E4, M19 |
| Shortcuts | M20, D10 |
| Packaging / i18n / tests | G1–G4, M23–M25 |
| HIG menus / errors | G5, G7 |

**Not exhaustively line-covered:** every branch of `gallerylayout.cpp` pack algorithms (math correctness under extreme aspect ratios), every VIPS band edge case, GIO error strings on all platforms, live focus traversal with screen readers.

---

## Master priority list (all audit passes)

| Priority | ID | Summary |
|----------|-----|---------|
| 1 | H7 | HUD identity pulse on every status update |
| 2 | H8/H9 | Dangling handle pointer; undo after item destroy |
| 3 | H6 | Image-mode rotation wiped on navigate |
| 4 | H3a | LoadAdd cancels LoadReplace via shared gen |
| 5 | H2 | Dual chrome input ownership |
| 6 | M20 | Ctrl+− zoom vs opacity |
| 7 | M7a | Slideshow continues in Gallery |
| 8 | M4 | Gallery zoom policy |
| 9 | M17/M18 | Canonical paths; VIPS autorot |
| 10 | M23–M26 | AppStream, tests, i18n, open errors |
| 11 | Docs | DOMAIN/TODO sync |

---

## Sign-off (deep pass 4)

Infrastructure and HIG surface reviewed. No product code modified. Audit document is the accumulated record (`AUDIT.md`). Further passes would need **runtime** testing (ASan for H8/H9, manual Gallery zoom, screen reader) rather than more static reading of the same tree.

---

## Deep pass 5 (continuation)

**Focus:** `GalleryLayout::pack` math, workspace snapshot vs duplicates, shape/hit residuals, DOMAIN invariants cross-check.  
**Note:** Static coverage of this tree is near saturation; this pass closes the gaps called out in pass 4.

| Step | Activity | Status |
|------|----------|--------|
| H1p | SideBySide / Vertical / Grid / GridCrop / Masonry / MasonryRows | [x] |
| H2p | Workspace snapshot/`m_itemStates` vs duplicate paths | [x] |
| H3p | `handleDrawSize` / shape padding vs viewport paint | [x] |
| H4p | DOMAIN invariants vs implementation checklist | [x] |
| H5p | Residual risk / what only runtime can prove | [x] |

---

### H1p — Gallery packing

All modes guard native dimensions with `qMax(1.0, …)` before division — **no div-by-zero** on empty pixmaps.

| Mode | Behaviour | Notes |
|------|-----------|--------|
| SideBySide | Scale to `availH`; advance x | Very wide total width → horizontal scroll; OK |
| Vertical | Scale to `availW`; advance y | Tall strip; OK |
| Grid | `cols = gridColumns or ceil(sqrt(n))`; cell fit min scale | Empty `n` not called from applyLayout when items empty |
| GridCrop | Uniform cell; `setGalleryCellSize` for clip | Depends on item paint clip — implemented |
| Masonry | Column shortest-top placement | Classic; column count clamped ≥1 |
| MasonryRows | Row shortest-left | Symmetric to masonry |

| ID | Severity | Issue |
|----|----------|--------|
| L32 | Low | **SideBySide / Vertical ignore `availW` / `availH` on the long axis** — by design (overflow scroll). Extreme aspect ratios produce huge scene rects; performance depends on FullViewportUpdate (already costly). |
| L33 | Low | Pack **forces rotation 0 and opacity 1** for all tiles — correct for Gallery; any desire to preview workspace orientation in Gallery is out of scope / DOMAIN. |

No algorithmic crash found in static read of `gallerylayout.cpp`.

---

### H2p — Snapshot identity vs DOMAIN duplicates

DOMAIN: *Duplicate selection → new canvas objects, **same paths**, independent transforms.*

Implementation:

- `m_itemStates` and `rememberItemState` are **`QHash`/`insert` by path** → **one state per path**.
- `m_savedWorkspace` is a **list** (can hold two entries with the same path).
- `findItemByPath` returns the **first** match.
- `LoadRestore` / `LoadAdd` skip if `findItemByPath` hits.

| ID | Severity | Issue |
|----|----------|--------|
| **M27** | Medium | **Duplicates are first-class on the canvas but second-class in persistence.** Leaving Workspace and returning may restore only one object per path; the other transform is lost or merged via hash. Matches earlier M16 theme. |
| M27b | Low | `snapshotWorkspace` list + hash dual structure is easy to desync when editing one path only. |

---

### H3p — Shape vs chrome paint

- Chrome **drawing** is viewport-space in `paintEvent` (scale-invariant).
- `shape()` / `boundingRect` still expand by **local** pads using `deviceScaleMin()` so Qt can deliver events near handles.
- View-owned hits are primary; shape expansion is supporting.

| ID | Severity | Issue |
|----|----------|--------|
| L34 | Low | Residual complexity: three notions of “handle size” (viewport draw px, local shape pad, hit radius). Documented in AGENTS; still a footgun for future edits (ties to H2). |

---

### H4p — DOMAIN invariants checklist

| Invariant | Status |
|-----------|--------|
| One mode at a time (`ViewMode`) | **Held** |
| Image canvas ≤1 image | **Held** (clear+replace load) |
| Gallery no object move/scale chrome | **Held** (flags + pack) |
| Workspace free transform | **Held** |
| Framing must not clear rotation | **Violated by H6** on navigate reload |
| Snapshot workspace on leave | **Attempted**; weakened by M27/H9 |
| Slideshow Image-only | **Violated / extended** (Gallery start) — docs drift H1 |
| Session order independent of canvas z | **Held** |

---

### H5p — Runtime-only residual risks

| Risk | Why static analysis is insufficient |
|------|-------------------------------------|
| H8/H9 UAF | Needs ASan + mode switch during handle drag / Undo after restore |
| H3a load cancel | Needs slow disk + rapid Next + drop |
| Gallery zoom + resize | Needs interactive pack timing |
| HiDPI chrome | Needs devicePixelRatio 2/3 screenshots |
| AT-SPI / keyboard-only | Needs screen reader run |
| GIO default-app | Needs desktop session |

---

## Audit completeness statement

| Category | Coverage |
|----------|----------|
| All `src/*.cpp` / `*.h` | Read in one or more passes |
| DOMAIN / TODO / AGENTS / REUSE / desktop / Nix / CMake | Reviewed |
| Pack algorithms | Reviewed for structure and div-safety |
| Automated proof of absence of races | **Not** done |
| Product code changes from audit | **None** |

**Recommendation:** Treat `AUDIT.md` as the backlog. Implementing **H7 → H8/H9 → H6 → H3a** yields the highest correctness return; then M-level UX. Further static passes on the same revision will mostly restate these items.

---

## Sign-off (deep pass 5)

Gallery pack and workspace identity reviewed. Master priority list unchanged. Static audit of tip `03926e4` lineage considered **complete** for non-runtime analysis.


---

## Implementation log (2026-08-30)

| ID | Status | Notes |
|----|--------|--------|
| H7 | Fixed | `setSessionPosition` pulses only when `changed && pulseIdentity` |
| H8 | Fixed | `m_handleDragItem` / anchors nulled in clearWorkspace, setWorkspacePaths, Delete, clearExtras |
| H9 | Fixed | `clearWorkspace` clears `m_undoStack` before destroying items |
| H6 | Fixed | LoadReplace reapplies rotation/flips from `m_itemStates`; rotate/flip remember state |
| H3a | Fixed | Only `LoadReplace` increments `m_loadGeneration` |
| M20 | Fixed | Opacity uses Ctrl+Shift+± |
| M7a | Fixed | `stopSlideshow` on Gallery enter and Workspace enter |
| H2 | Fixed | Item mouse/hover no longer drives handles; ImageView owns chrome |
| M4 | Fixed | zoomViewBy works in Gallery (pack still resets view on relayout) |
| M15 | Fixed | enterGalleryMode: populate then single enterGallery |
| M17 | Fixed | expandPaths uses canonicalFilePath for session membership |
| M18 | Fixed | VIPS load applies vips_autorot (EXIF orientation) |

| M26 | Fixed | statusBar message on empty open/add and failed decode |
| M27 | Fixed | LoadRestore uses pending state queue — duplicate paths restore |
| M23 | Fixed | data/qimgview.metainfo.xml + CMake install to metainfo |
