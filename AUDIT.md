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
