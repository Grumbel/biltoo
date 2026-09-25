<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Biltoo glossary

Short definitions of terms used in biltoo (and closely related thumtoo host
language). Normative behaviour lives in [DOMAIN.md](DOMAIN.md),
[IDENTITY.md](IDENTITY.md), [SIZE.md](SIZE.md), and
[docs/THUMTOO_HOST_CONTRACT.md](docs/THUMTOO_HOST_CONTRACT.md). This file is a
map of vocabulary, not a second law book.

Related brainstorm: [docs/SCENE_LANGUAGE_BRAINSTORM.md](docs/SCENE_LANGUAGE_BRAINSTORM.md).

---

## Product and domain

| Term | Meaning |
|------|---------|
| **Biltoo** | Desktop image/document viewer and session workbench (Qt). Not a file manager; not a full raster editor. |
| **Thumtoo** | Shared library/cache: sizes, durable tiles, LQIP, archive TOCs, optional tags. Biltoo is a primary host. |
| **Dirtoo** | Related media-oriented file manager (sibling project); not part of this binary. |
| **Session** | Ordered working set of **session images**. The user’s current list, not “whatever is on disk.” |
| **Session image** | One row in the session: decode **path** + independent **content appearance**. Identity is **SessionImageId**, not the path string. |
| **Session cursor / current index** | Which session row is “current” for Image mode, filmstrip highlight, and navigation. |
| **Canvas** | Main view surface (`ImageView` / `QGraphicsScene`). Always in exactly one **mode**. |
| **Mode** | Mutually exclusive presentation: **Image**, **Gallery**, or **Workspace**. Shell UI must not diverge from `ImageView` mode. |
| **Image mode** | Focus on one session image: view framing (fit/fill/1:1/zoom/pan) + content edit (crop/flip/90°). At most one canvas image. |
| **Gallery mode** | Whole session laid out by a **layout** (packed poses). Activate a tile → Image; return restores layout/scroll. |
| **Workspace mode** | Free placement of zero or more session images: position, scale, free rotate, opacity, z-order. Linear slideshow is off. |
| **Layout (Gallery)** | Pure arrangement algorithm: positions and cell scales from session order + params (margin, gap, columns). Not free-form. |
| **Layout panel (Workspace)** | Dock/toolbar: apply a packaged packer to the **current selection** only; does not enter Gallery. |
| **Filmstrip / thumbnail bar** | Strip of session rows (1:1 with document order); navigation and drop source for Workspace. |
| **Slideshow** | Timed advance through the session (Image framing). May start from Gallery (enters Image first). Not in Workspace. |
| **Project (`.biltoo`)** | Saved session: ordered ids/paths, content appearance, optional Workspace poses, assets/hashes. Does not modify source files. |
| **Non-destructive** | Edits are session/project appearance only; original files on disk are never overwritten by biltoo. |
| **Export** | Render canvas/region (e.g. Workspace bounds or **page guide**) to PNG/PDF. |
| **Page guide** | Optional Workspace framing rectangle for composition/print/export. |
| **HUD** | On-view status overlay (quality, path hints, etc.). |
| **Chrome** | UI handles and affordances drawn over items (crop grips, rotate, scale, opacity) — especially Workspace. |
| **Edge affordance** | Screen-edge hit zones (e.g. prev/next, return to Gallery/Workspace). |

---

## Identity and session data

| Term | Meaning |
|------|---------|
| **SessionImageId** | Stable `qint64` id of a session image. Never 0; never reused after remove. Key for appearance, crop, filmstrip overrides, canvas bind. |
| **List index** | Position `0…n-1` in session order. Changes on insert/delete/sort. Navigation and pack order use it; identity does not. |
| **Path** | Filesystem (or archive-member) string used to **decode** pixels. Same path may appear multiple times; not appearance identity. |
| **Duplicate (session)** | Second session row with the same path and a **new** SessionImageId; independent crop/appearance. |
| **SessionDocument** | Authoritative parallel `paths()` ∥ `ids()` lists for the session. |
| **Content appearance** | User-facing orient/crop/flip (and related) for one session image. Keyed by SessionImageId. |
| **Content id** | Identity of **source bytes** (e.g. content hash). Same file → same content id. See [CONTENT-VARIANT.md](CONTENT-VARIANT.md). |
| **Variant id** | Identity of one **edited view** of content; in practice SessionImageId. |
| **Bind / unbound** | Canvas item tied to a SessionImageId vs not yet associated. |
| **Stash / snapshot** | Saved canvas state when leaving a mode (Gallery pack memory, Workspace free-form poses) for return. |
| **Warm stash** | Return path that restores items without a full re-pack when valid. |
| **Pack order** | Session order as used by Gallery packing / reorder overlays. |
| **Path order** | Session path list used for navigation and open; kept aligned with ids. |

---

## Gallery layouts and packing

| Term | Meaning |
|------|---------|
| **Pack** | Compute poses for session items under the active Gallery layout. |
| **PackPose** | Centre (and scale) for one item in scene coordinates after packing. |
| **availW / availH** | Client width/height used as the packing viewport (live viewport when bars are AlwaysOn). |
| **Margin / gap** | Layout params: outer inset and spacing between cells. |
| **Side-by-side** | Horizontal strip; height fills available height. |
| **Vertical** | Vertical strip; width fills available width. |
| **Grid** | Regular rows/columns; cell size from available width and column count. |
| **Grid Crop** | Square cover-crop cells (UI currently disabled; conflicts with session crop). |
| **Masonry** | Column- or row-based uneven packing. |
| **Masonry Fill / Rows Fill** | After pack, scale columns/rows so the outer shape is a clean rectangle. |
| **Flow / Flow Fill** | Session order L→R, T→B, wrap at width; Fill justifies each row. |
| **Facing** | Book-style: cover alone, then two-up spreads (verso\|recto). |
| **Virtual plan / virtual slots** | Lightweight per-item bounds for large sessions; live `ImageItem`s only in a window. |
| **Virtual window / sync** | Which slots are materialised as live items from the plan. |
| **Placeholder** | Drawn stand-in (LQIP or offline chrome) for non-live virtual slots. |
| **EnterGallery / ExplicitLayout** | Pack reasons: mode entry vs user-driven relayout (affects restore/snapshot rules). |
| **Free-form** | Workspace-style placement; Gallery layouts are not free-form. |

---

## View, scene, and Qt surface

| Term | Meaning |
|------|---------|
| **ImageView** | Main `QGraphicsView` host: mode, scene, controllers (gallery, image, workspace, display pipeline). |
| **ImageItem** | Canvas object for one image (pixmap/tiles, transforms, sessionId). |
| **sceneRect** | Scrollable bounds of the scene (or view override). Gallery uses scene-level authority; stray view overrides cause scroll bugs. |
| **Viewport** | Visible widget area inside scrollbars. |
| **AlignCenter** | Qt alignment when the whole scene fits in the viewport; irrelevant once content exceeds the viewport. |
| **ScrollBarAsNeeded / AlwaysOn / AlwaysOff** | Qt bar policy. Gallery uses **AlwaysOn** when bars are enabled so pack width matches the live client. |
| **centerOn** | Scroll so a scene point sits at the viewport centre. |
| **View transform** | Zoom/pan of the *view* (Image/Workspace), distinct from item placement and content orient. |
| **Fit / Fill / 1:1** | View framing presets in Image mode; may stick across prev/next until unlocked. |
| **Logical size / intrinsic size** | Authoritative pixel geometry of the source (path). Soft samples never define it. See [SIZE.md](SIZE.md). |
| **Provisional size** | Stand-in geometry before durable size is known (e.g. fixed long-edge aspect). |
| **contentRect** | Logical box on the item into which samples are stretched. |
| **Content space** | Coordinates after content orient/crop; chrome and PDF link rects must map correctly here. |
| **Item / local space** | Coordinates in the item’s local frame before view transform. |
| **View / screen space** | Widget pixels after view transform. |

---

## Content edit and Workspace chrome

| Term | Meaning |
|------|---------|
| **Crop (session)** | Non-destructive crop rect (+ optional crop rotation) on a session image. |
| **Crop draft** | In-progress crop UI before Apply; Cancel/Esc discards. |
| **Content rotate** | 90° turns of the image content (not free Workspace tilt). |
| **Flip H/V** | Mirror content appearance. |
| **Reset appearance** | Clear user content transforms for the target session image(s). |
| **Free rotate / tilt** | Arbitrary angle on a Workspace object (placement), distinct from content 90°. |
| **Scale handle** | Workspace chrome to resize an object. |
| **Shear / parallelogram pose** | Non-uniform corner placement on Workspace items (when enabled). |
| **Opacity** | Workspace object transparency. |
| **Raise / Lower** | Stacking order (z) on Workspace. |
| **Selection (session)** | Current index / multi-select for filmstrip and nav. |
| **Selection (canvas)** | Which Workspace (or Gallery) objects receive transforms. |

---

## Pixels, cache, and quality

| Term | Meaning |
|------|---------|
| **Sample** | Any decoded bitmap used for display only; must not write logical/intrinsic size. |
| **LQIP** | Low-quality image placeholder from thumtoo Store (small). Host must not *request encode* of LQIP; use only if already present. |
| **Soft** | Small whole-image preview band (historical PreferCache soft encode). Product underlay preference is LQIP + **tiles**. |
| **Ladder** | Multi-resolution whole-image steps (debug/low-level; not the main user-visible prepare story). |
| **Tile / durable tile** | Cached pyramid cell (e.g. 256²) in thumtoo Store for zoomable display. |
| **Tile pyramid** | Full durable multi-scale tile set for a path. |
| **TileSynth** | PreferCache delivery synthesised from durable tiles. |
| **PreferCache** | Thumtoo request mode: prefer cached/synthesised rasters over a cold full decode. |
| **Full** | Near-native / full decode path when tiles/soft are insufficient. |
| **ImageCache** | Process RAM map path → best sample held in the host. |
| **PathRasterService** | Host scheduler of per-path want/have/climb (soft → PreferCache/TileSynth → optional Full). |
| **Display pipeline** | Coordinator installing samples/tiles into items and gallery underlays. |
| **Tile LOD** | Level-of-detail: which tile scale to show for the current view. |
| **Decode window** | Set of gallery items scheduled for pixel work given the viewport (+ overscan). |
| **Underlay** | Low-res image under live tiles/items (LQIP or soft) so cells aren’t empty. |
| **Prepare (tiles)** | User/host run to generate missing durable tiles (see prepare dialog / `thumtoo-prepare`). |
| **min_scale / detail level** | How deep a prepare run builds the tile pyramid (user-facing wording may hide raw scale). |
| **Store** | Thumtoo on-disk durable cache (tiles, LQIP, sizes, …). |
| **Client (thumtoo)** | API handle into thumtoo; modern hosts are Store-oriented. |
| **Warm cache** | Needed pixels already in ImageCache/Store; host must not re-schedule work. |
| **TTFP** | Time to first paint (diagnostics). |
| **GUI budget** | Scoped limit on GUI-thread work; log with `BILTOO_GUI_BUDGET_LOG`, abort with `STRICT`. |

---

## Shell, tools, and panels

| Term | Meaning |
|------|---------|
| **MainWindow** | Application shell: session, actions, docks, slideshow, open/save. |
| **Metadata panel** | File/document metadata display. |
| **TOC panel** | Table of contents (e.g. PDF outline) for jump navigation. |
| **Adjustments panel** | Colour/contrast-style session appearance controls. |
| **Help panel** | In-app help. |
| **Thumbnail bar** | See filmstrip. |
| **Tool (ImageView)** | Interaction tool enum (pan, crop, …) where used. |
| **Drag (Gallery reorder)** | Reorder session by dragging tiles; updates document order. |
| **Open / append session** | Replace or extend the session path/id lists. |
| **Return to Gallery / Workspace** | After open-from-mode, restore the remembered scene (not the other mode). |

---

## Documents and containers

| Term | Meaning |
|------|---------|
| **Archive** | Zip/tar/7z/RAR/… opened without unpacking; members become session paths via thumtoo TOC. |
| **Multi-page document** | PDF, DjVu, EPUB, … exposed as multiple session images (pages/surfaces). |
| **Spine (EPUB)** | Default linear reading order of content documents. |
| **Nav document (EPUB)** | TOC / landmarks machine-readable navigation. |
| **Outline (PDF)** | Bookmark tree → jump to pages. |
| **Link annotation (PDF)** | In-page hotspot → internal dest or URI. |
| **Named destination** | Stable PDF jump target. |

---

## Scene-language brainstorm (aspirational terms)

Used in [docs/SCENE_LANGUAGE_BRAINSTORM.md](docs/SCENE_LANGUAGE_BRAINSTORM.md); **not** shipped product vocabulary yet.

| Term | Meaning |
|------|---------|
| **Scene description language (SDL)** | Hypothetical data format for scenes, media refs, and navigation actions. |
| **Stack** | HyperCard-like document: media + scenes + links. |
| **Scene / card** | One presentation state (overview, focus, custom). |
| **Scene kind** | Typed runtime: gallery / image / workspace (preserves domain law). |
| **Generator** | Compiler from dir/archive/PDF/EPUB → scene document. |
| **Action (`go`, `return`, …)** | Closed navigation/behaviour vocabulary (not general scripting). |
| **Transclusion** | Media defined once, referenced from many scene placements (Nelson). |
| **Two-way / reverse link** | Synthetic back-edges where source formats only store A→B. |

---

## Ted Nelson (reference only)

| Term | Meaning in this project’s discussions |
|------|----------------------------------------|
| **Hypertext / hypermedia** | Non-linear documents with first-class links. |
| **Transclusion** | Inclusion by reference, not copy. |
| **Xanadu** | Nelson’s long-running hypertext design (two-way links, versioning, parallel docs). |
| **Intertwingularity** | Everything is interconnected — use as a design caution, not a mandate to merge modes. |

---

## Engineering / process

| Term | Meaning |
|------|---------|
| **AGENTS.md** | Standing rules for humans and automated agents on this repo. |
| **TODO.md** | Tip, open work, handoff; must suffice to continue without chat history. |
| **Bundle (`git bundle`)** | Catch-up package of commits from an agreed base to tip; deliverable for agent work. |
| **Tip** | Current HEAD of the active work line. |
| **Host** | Biltoo (or other app) code that calls thumtoo and owns UI/geometry policy. |
| **GUI thread** | Qt main thread; pixel-heavy work should not block it beyond budget. |
| **Characterization test** | Test that locks current behaviour so refactors don’t silently change it. |

---

## Quick “do not confuse” pairs

| Don’t mix | Why |
|-----------|-----|
| **Path** vs **SessionImageId** | Same file can be two session images with different crops. |
| **List index** vs **SessionImageId** | Index shifts; id does not. |
| **View zoom** vs **Workspace scale** vs **content size** | Three different transforms. |
| **Content 90°** vs **Workspace free rotate** | Appearance vs placement. |
| **Gallery layout** vs **Workspace layout panel** | Whole-session pack vs selection-only pack; different modes. |
| **Logical size** vs **sample / LQIP / soft pixels** | Geometry vs texture. |
| **LQIP** vs **request encode LQIP** | May *display* cache LQIP; must not *force* encode as a product path. |
| **Remove from session** vs **remove from Workspace canvas** | Session delete drops the row; canvas-only delete keeps the session image. |
| **sceneRect** (scene) vs **setSceneRect on the view** | View override was a source of Gallery scroll bugs. |
| **AsNeeded bars** vs **AlwaysOn (Gallery)** | AsNeeded can cover pack edges; Gallery packs to the post-bar viewport. |

---

*If you introduce a new public domain term in code or UI, add a row here in the same change.*
