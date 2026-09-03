# QImgView TODO

## Core Vision

QImgView is a classic Qt image viewer with three presentation modes on one canvas:

- **Image** — single-image browsing (zoom, pan, edge nav, slideshow)
- **Gallery** — session overview with packaged layouts (not free-form editing)
- **Workspace** — free-form multi-image canvas (move, handles, opacity, z-order)

## Version plan

### 0.1.0 — stable viewer (current feature set)

Ship what exists today as a reliable release. **No new editing features** in this
line beyond bugfixes, polish, and docs.

Focus:

- [ ] Stabilize Image / Gallery / Workspace interactions and mode transitions
  - [x] Gallery → Workspace must restore durable Workspace snapshot (or empty), never import Gallery packing
  - [x] Explicit Workspace **Layout** panel (selected tiles → packaged arrangement, Apply, columns/rows); Gallery mode switches stay Gallery-only
  - [x] Thumbnail bar visibility is **per mode**: Workspace default **on**, Gallery default **off**;
        toggling in one mode must not force the other. First enter of Workspace shows the strip.
  - [x] Layout panel: **hidden by default**; never visible in Gallery/Image; toggle action
        enabled only in Workspace (greyed out otherwise). Remember Workspace preference only.
- [ ] Session-image identity acceptance (see SESSION.md §4–§5):
  - [x] Duplicate / drop-duplicate: flip/crop one tile does not affect the other
  - [ ] Image-mode crop of slot B updates only Workspace tile B after return
  - [x] Gallery open of a duplicated path uses session id (not first path match)
  - [x] Session remove undo restores stable ids / Workspace association
  - [x] No unbound Workspace tiles after normal add/dup/drop paths
- [x] Build fix: m_cropTargetItem is ImageItem* (not QPointer); ImageItem is not QObject
- [ ] Fix remaining crashes, layout edge cases, and selection/context-menu bugs
- [ ] Re-enable Gallery Grid Crop once it coexists cleanly with session/manual crop
- [x] Crop: rotatable draft frame (bakes to axis-aligned output; cropRotation in session/project)
- [x] Crop: centre move grip; interior rubber-band; resize under rotation
- [x] Crop: Expand beyond image (pad); Close/Cancel chrome; modifiers (Ctrl/Shift)
- [x] Crop: re-enter with rotated draft keeps frame position (no flip remap / no constrain shift)
- [x] Workspace Layout panel (selection pack + Apply); durable Workspace vs Gallery
- [x] Masonry Fill / Masonry Rows Fill (rectangular pack)
- [x] Page Guide under Workspace menu; Exit Fullscreen label; FS Slideshow context
- [x] Quit confirm once (closeEvent only)
- [ ] Keyboard shortcuts, HUD, slideshow, and desktop integration as documented
- [ ] Packaging (Nix/CMake), AppStream, `.desktop`, i18n scaffolding
- [x] ProjectFile save/load round-trip unit tests (Qt Test)
- [ ] Regression smoke tests (`--help`, basic open/session paths)
- [ ] Drop the `-dev` suffix on `VERSION` when ready to tag

Rotate/flip in the UI remain **view/session transforms** only until 0.2.0
persist/export lands.

### 0.2.0 — non-destructive quick edits (future)

Goal: a small set of **viewer-grade** edits — not a GIMP substitute. Edits are
**non-destructive** while working (adjustable stack / sidecar) and can be
**written permanently** on explicit save/export (sidecar next to the file
and/or a derived image). Prefer lossless paths where the format allows
(e.g. JPEG orientation).

#### Design constraints (0.2.0)

- [ ] Edit stack per path (ordered ops); undo/redo within the stack
- [ ] Sidecar or XMP-style persistence so originals stay intact until export
- [x] Project **Save** / **Open** (`.qimgview`) — session + appearance + poses
- [x] **Export PNG** (content or page guide, chosen width)
- [ ] Explicit per-file Save / Export overwrite vs derived file vs sidecar-only
- [ ] Preview in Image mode; optional badges in Gallery for “has edits”
- [ ] Batch apply to Gallery multi-select where the op is unambiguous
- [ ] Keep the UI shallow: few tools, keyboard-friendly, no layer system

#### Operations — priority candidates

**Geometry (first)**

- [ ] **Rotate / flip — permanent** — persist today’s ±90° / H/V flip (lossless
      JPEG orientation when possible; otherwise rewrite pixels on export)
- [ ] **Crop** — free rectangle, aspect presets (1:1, 4:3, 16:9, original),
      rule-of-thirds guides; non-destructive until export
- [ ] **Straighten** — fine rotation (± a few degrees) with optional
      auto-horizon later; distinct from 90° snaps
- [ ] **Resize / export bounds** — max edge or megapixels on export only
      (does not replace a full image editor’s resample UI)

**Annotation & markup**

- [ ] **Text annotation** — one or a few labels, font size/colour, optional
      background pill; not a full typesetter
- [ ] **Basic drawing** — pen, highlighter, arrow, rectangle, ellipse;
      stroke colour/width; no layers or vector node editing
- [ ] **Callout / number pins** — quick “1, 2, 3” markers for review feedback
- [ ] **Blur / redact region** — privacy boxes (solid or pixelate) for
      screenshots and shareable exports
- [ ] **Watermark** — simple text or image stamp, corner/centre presets,
      opacity; aimed at share exports

**Tonal quick adjusts (optional, keep minimal)**

- [ ] **Brightness / contrast** or a single **Exposure** slider
- [ ] **White balance** — temperature/tint or click-grey
- [ ] **Saturation / vibrance** — one control, not a HSL suite
- [ ] **Sharpen** — light unsharp or clarity; default off
- [ ] **Greyscale** — one-shot B&W for review or export

**Metadata-oriented (fits a viewer)**

- [ ] **Rating / colour label** — session + sidecar; filter in Gallery later
- [ ] **Caption / title** — short text bound to the file (XMP/IPTC when built
      with Exiv2)
- [ ] **Auto-orient on save** — bake EXIF orientation into pixels/sidecar so
      other apps agree (complement existing load-time autorot)

#### Explicitly out of scope for 0.2.0

Layers, masks, healing/clone, perspective warp, curves/levels full UI, raw
develop, plugins marketplace, multi-page PDF editing. Those belong in a real
editor; QImgView stays a **viewer with quick fixes**.

## Done (recent)

- [x] Gallery as its own ViewMode (not a Workspace layout)
- [x] Packaged layouts: Horizontal, Vertical, Grid, Grid Crop, Masonry, Masonry Rows
- [x] Gallery → Image on item open; **Up** returns with scroll/selection restore
- [x] No move/handles outside Workspace; checkerboard optional via Preferences
- [x] Configurable background colour / pattern
- [x] VERSION file + Nix/CMake version wiring; separateDebugInfo
- [x] Code layout: MainWindow / ImageView / ImageItem split TUs; GalleryLayout; icons helpers
- [x] Clear residual view between Gallery layout switches; remove superfluous Stack layout
- [x] DOMAIN.md language-level model; AGENTS architecture section
- [x] Single mode source of truth (`ViewMode` only; no MainWindow mode bool)
- [x] Mode-aware `setCurrentIndex` (Image loads; Gallery/Workspace focus only)
- [x] Gallery keyboard: spatial arrows, Home/End, Enter to open
- [x] Session helpers: `enterGalleryMode`, `showPathInImageMode`, `enterWorkspaceMode`
- [x] Workspace Duplicate (Ctrl+D); reset scale/rotation; device-space chrome
- [x] HUD identity pulse (filename / [i/n]); fit preserves rotation

## Audit fixes implemented (2026-08-30)

- [x] H7 HUD identity pulse only on session index change
- [x] H8/H9 clear handle/drag pointers + undo stack when destroying items
- [x] H6 persist Image-mode rotation/flip across navigation (`m_itemStates`)
- [x] H3a LoadAdd does not advance load generation (won’t cancel LoadReplace)
- [x] M20 Opacity Ctrl+Shift+± (no clash with Zoom Ctrl+±)
- [x] M7a Stop slideshow when entering Gallery or Workspace
- [x] H2 Chrome input owned only by ImageView
- [x] M15 Single enterGallery after populate
- [x] M4 Gallery toolbar zoom matches wheel (until pack)
- [x] M17 Canonical session paths
- [x] M18 VIPS EXIF autorot
- [x] M26 Load/open error feedback on status bar
- [x] M27 Workspace duplicate path restore queue
- [x] M23 AppStream metainfo
- [x] M19 Thumbnail file-drag in Workspace (no Alt required)
- [x] L27 Help → Keyboard Shortcuts (F1)
- [x] M24 CMake smoke test (`qimgview --help`)
- [x] M25 i18n scaffolding (translator load + CLI tr + translations/README)
- [x] L22 Status colour shows alpha when present

- [x] L29 Mode-filtered view context menu
- [x] Slideshow disabled status tip explains reason

## Open

- Full `.ts` locale packs (community)
- Deeper automated GUI tests
- [x] Broader undo coverage for transforms, crop, session remove, Duplicate
- [ ] Undo for Workspace membership hide/show, sort reorder, layout switch
- Optional confirmation dialogs for destructive session replace (drop/Open)

### Stability / polish (optional)

- [ ] Thumbnail click crash (needs reliable repro)
- [x] CLI `--mode=image|gallery|workspace` (overrides Preferences start-in-workspace for the launch)
- [ ] Drag-load status: show count of images still decoding
- [ ] AppStream screenshots for Flathub-style stores
- [x] Virtualize Gallery when sessions are huge (viewport decode window + placeholders)
- [ ] Colour-managed display pipeline (beyond libexiv2 metadata)

## Done in this series (handles → session UX, 2026-08-30)

### Workspace chrome (`HANDLES.md`)

- [x] View-space scale/rotate handles; constant screen size; rotate with image
- [x] Degenerate-frame guards; scene-projection edge scale near 0
- [x] Adaptive outside chrome (buttons / opacity); L-bracket corners
- [x] Fast-click deselection / double-click handling polish

### Session & modes

- [x] Thumbnail: click = select; double-click = Workspace membership; drag adds
- [x] Image-mode drop **appends** (Open still replaces); File → New empty session
- [x] Gallery first-open showed one tile — enter Gallery before `setWorkspacePaths`
- [x] Gallery thumb strip no longer force-select-all on enter
- [x] Gallery pack order follows session/sort order (not async load order)
- [x] Gallery scroll restore via scene-centre snapshot on Image round-trip
- [x] Gallery Delete removes from **session** (undoable); layout switch does not resurrect
- [x] Session-remove undo kept across canvas tile destroy (`preserveUndoOnDestroy`)
- [x] Gallery → Image: stop layout debounce; no double decode of random first tile

### Gallery interaction

- [x] Rotate/flip selected tiles; right-click keeps multi-select for context menu

- [x] Ctrl/Shift/rubber-band multi-select; preserve selection across layout switch
- [x] Ctrl+click multi-select not cleared by `focusSessionPath` (use `revealGalleryPath`)
- [x] Wheel **scrolls only** (no zoom); Horizontal/Masonry Rows map vertical wheel → H-scroll
- [x] Zoom Fit/Fill use `fitInView` on packed bounds
- [x] Grid cells width-driven (fewer columns → larger tiles); vertical scroll
- [x] Sort: name, date, file size, width, height, pixel count; toolbar menu; repacks Gallery

### Workspace / chrome infra

- [x] `destroyCanvasItem` centralizes UAF-safe deletion
- [x] Workspace sceneRect expansion for middle-click pan / scrollbars
- [x] Workspace rubber-band on Select tool

### Slideshow / packaging

- [x] `[` / `]` slower/faster interval (mpv-style)
- [x] `[` / `]` no longer conflict with rotate (Ctrl+L / Ctrl+R / R only)
- [x] Interval range 0…60 s (milliseconds; 0 = max speed)
- [x] Adaptive step sizes (fine near 0, coarser at long dwells)
- [x] HUD flash shows current interval on `[` / `]` and on start
- [x] Preferences + CLI accept 0 ms / sub-second values
- [x] Pinned HUD: 1px bottom progress line during slideshow dwell
- [x] Workspace toolbar: Select/Pan only (no Undo/Raise/Duplicate)
- [x] Context menu: Open Selection in New Window; Duplicate; no Workspace toggle
- [x] Drag/open decode status: pending image count on status bar + HUD line
- [x] flake `apps.default` `meta.description`
- [x] `.desktop` uses `%F` for multiple images



## Workspace project files (`.qimgview`)

Workspace is an **ad-hoc** “put pictures next to each other” canvas — not a
print-paper document. The page guide is optional chrome for print/PDF, not the
default frame of meaning.

### Goals

- [x] **Load / Save project** (File → Open/Save Project) — `.qimgview` JSON
- [x] **Non-destructive** — project stores session appearance + optional free-form
  poses; never writes into source image files or source directories by default
- [x] **Content addressing** — each referenced file records **SHA-256**; load
  verifies the path or searches near the project file for a matching hash
- [x] **Identity** — session rows keep `SessionImageId`; duplicates stay independent
- [x] Export PNG (page guide or content bounds + resolution) — separate from project
- [x] Fit page guide to content — export helper only, not a print default

### Format sketch (version 1)

```json
{
  "format": "qimgview-project",
  "version": 1,
  "mode": "workspace",
  "assets": [
    { "sha256": "<hex>", "path": "/abs/…", "pathRelative": "optional/rel" }
  ],
  "images": [
    {
      "id": 1,
      "asset": "<sha256>",
      "workspace": true,
      "appearance": { "contentHFlip": false, "x": 0, "y": 0, "scaleX": 1, "…" }
    }
  ],
  "pageGuide": { "visible": false, "widthMm": 210, "heightMm": 297 }
}
```

Relink UI: prompt to locate missing assets on load; warn on SHA-256 mismatch.

### Session identity (Workspace / duplicates)

- Canvas membership (thumbnail double-click) keys on **SessionImageId**
  (`detachCanvasSessionId` / `addImageForSession`), never path occurrence.
- Bound tiles do not write content appearance into the path map.
- Session-bound canvas add decodes fresh (no clone of peer baked pixels).


## Interaction summary (current)

| Input | Behaviour |
|-------|-----------|
| Click item (Gallery) | Select (exclusive) |
| Ctrl/Meta+click (Gallery) | Toggle in multi-select |
| Shift+click (Gallery) | Range from anchor |
| Drag empty (Gallery / Workspace Select) | Rubber-band multi-select |
| Double-click (Gallery) | Open in Image mode |
| Delete (Gallery) | Remove from session (undoable) |
| Delete (Workspace) | Remove from canvas only |
| Wheel (Gallery) | Scroll only (strip layouts → horizontal) |
| Wheel (Image / Workspace) | Zoom view |
| Click thumb | Session select / navigate |
| Double-click thumb (Workspace multi-select) | Toggle canvas membership |
| Drag thumb → canvas | Add to session/canvas |
| `[` / `]` | Slideshow interval slower / faster |

See also [HANDLES.md](HANDLES.md), [DOMAIN.md](DOMAIN.md), [AGENTS.md](AGENTS.md).


## Active bugs (2026-09-02)

- [x] **History / loadFiles leaves orphan Workspace tiles**  
  loadFiles now calls setWorkspacePaths for Gallery **and** Workspace so
  id-mismatched tiles are pruned after a full session replace.

- [x] **Duplicate does not move selection to the new item(s)**  
  applyDuplicate selects the new session indices on filmstrip + canvas and
  scrolls the filmstrip to the first new row.

- [x] **Thumbnail bar shows wrong image after drag&drop**  
  Root cause: setFiles then setSessionIds left scheduleThumbnailLoads pairing
  rows with stale ids; path-level overrides also painted crops onto every
  path-matching row (including bound duplicates). Fixed with atomic
  ThumbnailBar::setSession(paths, ids) and path-override only for unbound rows.

- [x] **Duplicate 2nd+ selection / wrong source**  
  Path-occurrence reselect always copied the first tile with that path. Now
  sources and post-duplicate selection use SessionImageId.

- [x] **Filmstrip drag identity + drop-duplicate crop**  
  Store SessionImageId on each QListWidgetItem (RoleSessionId). mimeData reads
  item data. Drop-duplicate copies content appearance to the new session id.

- [x] **SessionImageId uniqueness guards**  
  SessionDocument refuses/reallocates duplicate ids; ImageView logs if two
  live/stashed items share an id; peer crop sync refuses mismatched paths;
  ThumbnailBar logs duplicate ids in setSessionIds.

- [x] **Two Workspace tiles, one SessionImageId**  
  Root cause: drop called bindSelectedSessionIds({sid}) on the current
  selection (original tile) while LoadAdd also bound the new tile to sid.
  Fixed: drop no longer bulk-binds selection; bindSelectedSessionIds and
  LoadAdd refuse an id already owned; rebind demotes the second claim.

- [x] **Gallery Crop (C / toolbar)**  
  Crop mode enabled in Gallery when a single tile is selected; removed
  Gallery hard-blocks in setCropMode / cropTargetItem.

- [x] **SessionImageId order on canvas**  
  `m_sessionIdOrder` parallel to `m_pathOrder`; placeOrMoveImageAt records
  membership by id so LoadAdd/drop-duplicate is not path-occurrence based.

- [x] **Adjustments panel** — brightness/contrast/sat/hue/gamma + histogram + vectorscope (SessionImageId).

- [x] **Colour grade survives Duplicate / thumbnail / reloads**  
  1. `duplicateSelected` applies `setColorAdjustments` on the copy and stores
     grade in pending appearance; bind-from-pending reapplies grade to the item.
  2. `sessionAppearanceImage` applies colour grade so ThumbnailBar overrides
     and drop-duplicate thumbs match the live tile; slider changes refresh thumbs.
  3. LoadRestore path sets `colorAdjustments` after content bakes.
  4. WorkspaceController restore always re-syncs grade from the session store
     (and again after pixel rebuild). Project save/load already serialized the
     five grade fields in WorkspaceItemState.

- [x] **Project load drops every session image onto Workspace**  
  Root cause: `loadProjectFromPath` called `setWorkspacePaths(full session)` so
  every session row became a live tile. Workspace canvas is a membership subset
  (double-click / drag); only images with `hasWorkspacePose` should be placed.
  Fixed: clear prior workspace (incl. durable snapshot + appearance store), enter
  Workspace, `addImageForSession` only for pose entries; LoadAdd applies placement
  from the appearance store. Gallery path unchanged (full session).

- [x] **Central content appearance apply (drop/thumb grade + less fragility)**  
  Added `SessionAppearance::applyContentToItem` — single order for crop → content
  bakes → chrome flags → colour grade. `applyStoredAppearance`, createItemFromImage
  (Image mode), LoadRestore, content reset, and Workspace restore all call it.
  `copySessionAppearance` falls back to a live donor when the store has no entry
  (filmstrip drop-duplicate). LoadAdd emits `sessionAppearanceChanged` after apply
  so thumbs stay graded. Grade-only rebind on already-decoded tiles applies in place.

- [x] **Content-appearance path audit (post-centralise)**  
  All decode/restore/rebind full-content sites use `applyContentToItem` or
  `applyStoredAppearance` (which delegates). Remaining intentional special cases:
  crop-mode enter (`applyContentBakes` without crop so the draft is full-frame);
  live chrome bakeFlip/bakeRotate90 then commit; `setTargetColorAdjustments`
  (interactive grade); `applyCropAppearance` (undo reconstruct from source image).

- [x] **Workspace Copy / Cut / Paste**  
  Ctrl+C/X/V and Workspace menu + context menu. Clipboard MIME
  `application/x-qimgview-workspace-items` (JSON via project appearance serde + path).
  Copy captures path + content + pose; Cut = copy + canvas-only remove (session
  kept, same as Delete); Paste allocates new SessionImageIds, offsets pose by
  (40,40), sets appearance store, places via `addImageForSession` / LoadAdd.

- [x] **Workspace background as project state + toolbar**  
  Per-Workspace override (`WorkspaceBackground`: AppDefault / Solid /
  Checkerboard / ImageTile). AppDefault uses Preferences (technical default)
  and is not written to the project. Workspace tools bar: two buttons
  (**Background…** dialog, **Background Default**). Saved as
  `workspaceBackground` in the project file; restored on load; cleared on New.

- [x] **Workspace background image portability**  
  Project stores `image` / `imageRelative` / `imageSha256` (when under project dir).
  Load resolves absolute then relative via `resolveWorkspaceBackgroundImage`.
  No `.assets/` embedding — JSON path + checksum only.

- [x] **Workspace background editor dialog**  
  Unified `WorkspaceBackgroundDialog` with live preview (solid / checker /
  image / app default). Toolbar: **Background…** opens dialog;
  **Background Default** resets to AppDefault (undoable).

- [x] **Audit follow-ups (clipboard / apply / background)**  
  - Paste enabled only when clipboard has Workspace MIME (`dataChanged`)  
  - Copy/Cut/Paste also on Edit menu  
  - Undo for Workspace Cut (canvas restore) and Paste (remove new session rows)  
  - `applyStoredAppearance` reloads full source before geometry content ops  
  - Background image `imageSha256` stored and checked on resolve

- [x] **Background polish (undo, LOD, live preview)**  
  - Undoable background changes (`WorkspaceBackgroundCommand`)  
  - Live canvas preview while the dialog is open (restore on Cancel)  
  - Image-tile LOD when zoomed out  
  - Path/checksum only in project JSON (no `.assets/` copy)

- [x] **Background references are JSON-only**  
  Suppress dialog `backgroundChanged` during programmatic `setBackground`.  
  Workspace background images are stored as path / pathRelative / imageSha256 in
  the project file — no `.assets/` copy or prune.

- [x] **Load images from archives (libarchive, in-memory)**  
  Path syntax: `file:///path/to/archive.tar//archive:member/path.jpg`  
  (one level only). Opening a `.zip`/`.tar*`/etc expands to member refs.  
  Decode via libarchive → QByteArray → QImageReader; no disk extraction.  
  Requires libarchive at build time (`QIMGVIEW_HAVE_ARCHIVE`).

- [x] **Archive follow-ups**  
  HUD/title/thumbnail labels use `ArchivePath::displayName`.  
  Open dialog default filter is **Images and archives**.  
  Project save hashes the archive container; member path is stored in appearance
  and rebuilt on load after the container is resolved.  
  WebP/etc. from archives: format-hint + libvips buffer fallback.  
  Session history must use `ArchivePath::canonicalSessionPath` (never
  `QFileInfo::absoluteFilePath` — collapses `//archive:`).


---

## Handoff (2026-09-03) — continue from tip / next bundle `qimgview-065-…`

**Git tip after stacks:** `e40b67c` — *Document handoff…* (docs only)  
**Code tip:** `85ac5d0` — *Fix LoadAdd creating extra tiles for already-bound session ids*  
**Requires previous tip:** `85ac5d0` before this docs commit; full feature stack ends at 063.

### Bundle stack (this workstream)

Apply in order under `/home/workdir/artifacts/` (each is `git pull <bundle> HEAD`):

| Bundle | Tip (approx) | Topic |
|--------|--------------|--------|
| `qimgview-040-colour-grade-persist` … `051-bg-json-refs-only` | | Grade persist, central appearance, clipboard, background JSON |
| `qimgview-052-archive-images` | | libarchive in-memory + `//archive:` syntax |
| `qimgview-053-archive-followups` | | displayName, dialog filter, project container hash |
| `qimgview-054-fix-build-session` | | qualify `ProjectFile::appearanceFromJson` |
| `qimgview-055-mark-dirty-public` | | `markWorkspaceDirty` public for undo cmds |
| `qimgview-056-fix-archive-decode` | | full `archive_read_data` loop + content sniff |
| `qimgview-057-archive-dialog-and-load-logs` | | default Open filter includes archives; qWarning on failures |
| `qimgview-058-fix-history-archive-paths` | | history must not `cleanPath` archive refs |
| `qimgview-059-archive-webp-decode` | | format hints + `vips_image_new_from_buffer` |
| `qimgview-060-adjustments-opt-in` | | Adjustments dock default hidden |
| `qimgview-061-icons-layout-prefs` | | coloured icons; layout on workspace bar; prefs chrome |
| `qimgview-062-export-bg-two-buttons` | | PNG export paints workspace bg; two bg buttons |
| `qimgview-063-fix-loadadd-bind` | `85ac5d0` | LoadAdd: no extra tiles for live session ids |
| `qimgview-064-handoff-docs` | `e40b67c` | This handoff documentation |

Author on all commits: `Ingo Ruhnke <grumbel@gmail.com>` with  
`Co-authored-by: Grok <grok@x.ai>`. Bundles use **HEAD** as ref and stack cleanly.

### Architecture notes (do not regress)

**Identity**

- Session identity is `SessionImageId`, never the path string alone.
- Paths may repeat; duplicates are separate session rows.

**Content appearance (central)**

- Content ops (crop, content flip/quarter-turn, colour grade) live on
  `WorkspaceItemState` / `SessionAppearanceStore`, keyed by `SessionImageId`.
- Apply via `SessionAppearance::applyContentToItem` (and helpers). Prefer this
  over ad-hoc `setColorAdjustments` / crop / bake calls at call sites.
- Placement (pos, scale, free rotation, opacity, z) is separate from content.

**Archive paths**

- Syntax: `/abs/path/archive.zip//archive:member/inside.jpg`  
  (optional `file://` prefix on the archive side is parsed; stored form is usually
  absolute path + `//archive:` + member).
- **Never** pass an archive ref through `QFileInfo::absoluteFilePath` /
  `QDir::cleanPath` — collapses `//` and breaks the marker.
  Use `ArchivePath::canonicalSessionPath` / `parse` / `makeRef`.
- One nesting level only. Expand archive → list of member refs in session.
- Decode: libarchive full read → `ImageLoader::decodeFromBytes` (Qt format
  candidates → sniff → libvips buffer). Build needs `QIMGVIEW_HAVE_ARCHIVE`
  (pkg-config `libarchive`). Runtime logs if missing.

**Workspace background**

- Project-owned `WorkspaceBackground` (AppDefault / Solid / Checkerboard / ImageTile).
- JSON fields: mode, colors, image path, relative path, sha256 — **no** `.assets/`.
- View + export share `ImageView::paintCanvasBackground`.
- Export PNG: transparent checkbox; when off, canvas background is painted first.

**LoadAdd / binds**

- Placeholders often already have `SessionImageId`. Pending binds for live ids
  must be dropped; tile count capped by `pathOrder` multiplicity
  (`imageview_load.cpp` LoadAdd). Do not reintroduce `wanted = have + pendingBinds`
  without that purge.

**Chrome defaults**

- Adjustments dock: opt-in (`adjustmentsPanelVisible`, default false); force after
  `restoreState`.
- Layout panel: Workspace-only preference `layoutPreferredInWorkspace`.
- Preferences → Interface exposes adjustments / layout / per-mode thumbnail defaults.

### Known gaps / next candidates

- [ ] Nested archives (out of scope by design for now)
- [ ] Cache decoded archive members (memory / LRU) if large zips feel slow
- [ ] Status bar / HUD when libarchive was not compiled in
- [ ] Image-mode crop of duplicate slot B → only tile B after return (see §0.1.0)
- [ ] Grid Crop re-enable
- [ ] Permanent rotate/flip / sidecar story (0.2.0)
- [ ] Verify Export PNG letterboxing vs page-guide white sheet interaction
- [ ] Regression pass: open zip with WebP, history reopen, project save/load with
      archive members + workspace background image

### Files to read first

| Area | Files |
|------|--------|
| Appearance | `src/sessionappearance.{h,cpp}`, `src/imageview_types.h` |
| Load / binds | `src/imageview_load.cpp` |
| Archives | `src/archivepath.*`, `src/archivereader.*`, `src/imageloader.cpp` |
| Background | `src/imageview_paint.cpp` (`paintCanvasBackground`), `src/workspacebackgrounddialog.*`, `src/projectfile.*` |
| Session / history | `src/mainwindow_session.cpp` (`rememberSessionHistory`, expandPaths) |
| Export | `src/mainwindow_print.cpp`, `ImageView::renderExportImage` |
| Rules | `AGENTS.md`, this file |

### Agent workflow reminders

- Document plans and progress in **TODO.md** (or linked notes) continuously.
- Small task-focused commits; next bundle number is **065** (`qimgview-065-…`).
- Prefer shallow checkout + rsync into artifacts; writing artifacts is slow.
- No feature rollback without discussion; no quick hacks — fix the model.


---

## Plan / work (2026-09-03) — bundle `qimgview-065-…`

**Task:** Verify Load/Save of cropping, workspace positions, colour grade, and
related project state; strengthen unit tests; hook tests into `nix flake check`.

### Findings (current tip `d394337`)

- `tests/projectfile_roundtrip.cpp` already round-trips crop rect/source/rotation,
  content flips/quarter-turns, and workspace pose (x/y/scale/rotation/opacity/z/flips).
- **Gaps:**
  1. `appearanceEqual` does **not** compare `ColorAdjustments` (brightness/contrast/
     saturation/hue/gamma). Colour grade can drift through save/load without failing
     the semantic test.
  2. `documentsEqual` does **not** compare `hasWorkspaceBackground` /
     `workspaceBackground` (mode, colors, image path/relative/sha256).
  3. No dedicated slot that forces non-identity colour grade + non-default
     workspace background through full save → load → equality.
  4. `nix flake check` only builds the package (`checks.qimgview = qimgview`);
     the Qt Test executable is built only when `Qt6Test` is found at configure
     time and is **not** run as a flake check.

### Plan

1. Extend `appearanceEqual` to require colour-grade field equality (ints +
   near-equal gamma).
2. Extend `documentsEqual` to require workspace-background equality (mode,
   colors, paths, sha256) when either side claims a project-owned background.
3. Add test slots:
   - `appearanceJson_colorGrade` — non-identity grade through
     `appearanceToJson` / `appearanceFromJson`.
   - `project_saveLoad_colorAndBackground` — full document with crop + pose +
     colour grade + solid/checkerboard/image-tile background fields.
4. Wire CMake/Nix so `nix flake check` runs `projectfile-roundtrip` (and keep
   the existing `--help` smoke test). Prefer a dedicated `checks.projectfile-test`
   derivation (or package `doCheck` + `ctest`) that depends on Qt6 Test without
   requiring a full GUI runtime.
5. Document completion in this section and bump handoff tip / next bundle to 066.

### Out of scope for this bundle

- Full GUI integration tests for Image-mode crop of duplicate slot B.
- Archive member + background-image file resolution on disk (unit test stays
  path/JSON only unless a tiny fixture is added later).

### Done in this bundle (`qimgview-065-…`)

- [x] `appearanceEqual` compares `ColorAdjustments` (brightness/contrast/sat/hue/gamma)
- [x] `documentsEqual` compares project-owned `WorkspaceBackground`
- [x] New slots: `appearanceJson_colorGrade`, `project_saveLoad_colorAndBackground`
      (crop + pose + grade + checkerboard/solid/AppDefault backgrounds; JSON stable
      with grade + solid bg)
- [x] `default.nix`: `doCheck = true` + `QT_QPA_PLATFORM=offscreen` so
      `nix flake check` / package check runs `ctest` (projectfile-roundtrip + help)
- [x] Local verification: all 8 Qt Test cases PASS under Qt 6.4.2

**Next bundle:** `qimgview-066-…` — remaining known gaps (duplicate Image-mode crop
sync, Grid Crop, archive regression pass, etc.).
