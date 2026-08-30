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
