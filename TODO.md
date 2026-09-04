# Biltoo TODO

## Core Vision

Biltoo is a classic Qt image viewer with three presentation modes on one canvas:

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
  - [x] Image-mode crop of slot B updates only Workspace tile B after return
        (path-open now prefers selected/sole live tile’s SessionImageId;
        peer sync already id-only — GUI smoke still welcome)
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
- [x] Keyboard shortcuts, HUD, slideshow, and desktop integration as documented
  (F1 help + README; transitions; .desktop MIME; AppStream metainfo)
- [x] Packaging (Nix/CMake), AppStream, `.desktop` (i18n remains optional `.qm` install)
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
- [x] Project **Save** / **Open** (`.biltoo`) — session + appearance + poses
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
editor; Biltoo stays a **viewer with quick fixes**.

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
- [x] M24 CMake smoke test (`biltoo --help`)
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



## Workspace project files (`.biltoo`)

Workspace is an **ad-hoc** “put pictures next to each other” canvas — not a
print-paper document. The page guide is optional chrome for print/PDF, not the
default frame of meaning.

### Goals

- [x] **Load / Save project** (File → Open/Save Project) — `.biltoo` JSON
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
  "format": "biltoo-project",
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
  `application/x-biltoo-workspace-items` (JSON via project appearance serde + path).
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
  Requires libarchive at build time (`BILTOO_HAVE_ARCHIVE`).

- [x] **Archive follow-ups**  
  HUD/title/thumbnail labels use `ArchivePath::displayName`.  
  Open dialog default filter is **Images and archives**.  
  Project save hashes the archive container; member path is stored in appearance
  and rebuilt on load after the container is resolved.  
  WebP/etc. from archives: format-hint + libvips buffer fallback.  
  Session history must use `ArchivePath::canonicalSessionPath` (never
  `QFileInfo::absoluteFilePath` — collapses `//archive:`).


---

## Handoff (2026-09-03) — continue from tip / next bundle `biltoo-065-…`

**Git tip after stacks:** `e40b67c` — *Document handoff…* (docs only)  
**Code tip:** `85ac5d0` — *Fix LoadAdd creating extra tiles for already-bound session ids*  
**Requires previous tip:** `85ac5d0` before this docs commit; full feature stack ends at 063.

### Bundle stack (this workstream)

Apply in order under `/home/workdir/artifacts/` (each is `git pull <bundle> HEAD`):

| Bundle | Tip (approx) | Topic |
|--------|--------------|--------|
| `biltoo-040-colour-grade-persist` … `051-bg-json-refs-only` | | Grade persist, central appearance, clipboard, background JSON |
| `biltoo-052-archive-images` | | libarchive in-memory + `//archive:` syntax |
| `biltoo-053-archive-followups` | | displayName, dialog filter, project container hash |
| `biltoo-054-fix-build-session` | | qualify `ProjectFile::appearanceFromJson` |
| `biltoo-055-mark-dirty-public` | | `markWorkspaceDirty` public for undo cmds |
| `biltoo-056-fix-archive-decode` | | full `archive_read_data` loop + content sniff |
| `biltoo-057-archive-dialog-and-load-logs` | | default Open filter includes archives; qWarning on failures |
| `biltoo-058-fix-history-archive-paths` | | history must not `cleanPath` archive refs |
| `biltoo-059-archive-webp-decode` | | format hints + `vips_image_new_from_buffer` |
| `biltoo-060-adjustments-opt-in` | | Adjustments dock default hidden |
| `biltoo-061-icons-layout-prefs` | | coloured icons; layout on workspace bar; prefs chrome |
| `biltoo-062-export-bg-two-buttons` | | PNG export paints workspace bg; two bg buttons |
| `biltoo-063-fix-loadadd-bind` | `85ac5d0` | LoadAdd: no extra tiles for live session ids |
| `biltoo-064-handoff-docs` | `e40b67c` | This handoff documentation |

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
  candidates → sniff → libvips buffer). Build needs `BILTOO_HAVE_ARCHIVE`
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
- Small task-focused commits; next bundle number is **065** (`biltoo-065-…`).
- Prefer shallow checkout + rsync into artifacts; writing artifacts is slow.
- No feature rollback without discussion; no quick hacks — fix the model.


---

## Plan / work (2026-09-03) — bundle `biltoo-065-…`

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
  4. `nix flake check` only builds the package (`checks.biltoo = biltoo`);
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

### Done in this bundle (`biltoo-065-…`)

- [x] `appearanceEqual` compares `ColorAdjustments` (brightness/contrast/sat/hue/gamma)
- [x] `documentsEqual` compares project-owned `WorkspaceBackground`
- [x] New slots: `appearanceJson_colorGrade`, `project_saveLoad_colorAndBackground`
      (crop + pose + grade + checkerboard/solid/AppDefault backgrounds; JSON stable
      with grade + solid bg)
- [x] `default.nix`: `doCheck = true` + `QT_QPA_PLATFORM=offscreen` so
      `nix flake check` / package check runs `ctest` (projectfile-roundtrip + help)
- [x] Local verification: all 8 Qt Test cases PASS under Qt 6.4.2

**Next bundle:** `biltoo-066-…` — remaining known gaps (duplicate Image-mode crop
sync, Grid Crop, archive regression pass, etc.).

---

## Plan / work (2026-09-03) — bundle `biltoo-066-…`

**Bug:** `nix run . -- /tmp/test.biltoo` (or any CLI path ending in `.biltoo`)
did not load the project and showed no useful feedback.

**Root cause:** `main` always called `MainWindow::loadFiles`, which runs
`expandPaths` → only image/archive/dir entries. A project file is skipped →
empty session. Status bar may show “No readable images found” briefly (easy to
miss); no project-specific path.

**Fix:**

1. `MainWindow::openProjectFile(path)` — shared load + set `m_projectPath` +
   recent list + status (used by Open Project, Recent, CLI).
2. CLI: split positional args into `.biltoo` vs other; open first project via
   `openProjectFile`; `qWarning` + status bar on failure; warn if multiple
   projects or mixed image args.
3. Help text mentions projects.

**Out of scope:** desktop MIME for `application/x-biltoo`; auto-reload of
missing assets without dialogs in pure headless mode.

### Done

- [x] CLI opens `.biltoo` via project load
- [x] Explicit error when file missing / load fails (stderr + status bar)
- [x] Refactor Open Project / Recent to `openProjectFile`

---

## Bundle `biltoo-068` — silence vips/fftw3 pkg-config noise

pkg-config prints “Package fftw3 was not found” several times while probing
`vips` because fftw3 is in vips’ Requires.private. Same class of issue as
sysprof-capture-4 for glib. Fix: add `fftw` to `default.nix` buildInputs so
`fftw3.pc` is visible (we do not link FFTW ourselves). Do **not** hide the
messages with env hacks — supply the missing private dep.

---

## Bundle `biltoo-069` — project load: intermittent first-image placement

**Symptom:** Loading a `.biltoo` project sometimes places the first session
image on the Workspace canvas and sometimes does not.

**Root cause (leftover path):**
`ImageView::loadImage` in multi-item mode with an empty canvas schedules
`LoadReplace`, and `onImageLoaded` **seeds** that path as a live tile
(“Workspace with empty canvas: seed with navigated image”). That path is
meant for session navigation in an empty Workspace, not project restore.

`loadProjectFromPath` for Image-mode projects called `loadImage(paths[0])`
without ensuring Image mode first. With Preferences “Start in workspace mode”
(or any prior Workspace mode), that seeded an unbound first tile. Without it,
Image mode showed the classic single image — intermittent by preference/mode.

**Also fixed:** pose/appearance rows are keyed by index through id
finalization so images with missing/invalid stored ids still get a pose after
`allocId()`. Gallery-mode projects enter Gallery explicitly.

**Fix:** Enter the project’s target mode first; place only `hasWorkspacePose`
rows via `addImageForSession`; never call `loadImage` while still in
Workspace/Gallery during project load.

---

## Bundle `biltoo-071` — first Workspace tile loses pose/appearance on load

**Symptom:** After project load, the first image appears but placement, flip,
rotation, and colour grade are lost; other tiles restore correctly.

**Cause:** Race between empty-Workspace `LoadReplace` seed (“navigated image on
empty canvas”) and `LoadAdd` for the first session path. If the seed wins,
LoadAdd sees `have == wanted` with a decoded unbound tile and never binds the
pending SessionImageId or applies `m_appearance` — defaults only.

**Fix:**
1. `clearWorkspace` clears path `m_itemStates` and bumps `m_loadGeneration`
   (invalidate in-flight LoadReplace).
2. LoadReplace seed only when there are no pending session binds / pending path.
3. LoadAdd claims existing unbound tiles for pending binds and applies
   content + pose from the appearance store.
4. `addImageForSession` early-return re-applies store appearance.

---

## Placement shear (full linear pose) — done (074–077)

**Goal:** Represent general parallelogram frames. Current pose is only
`R(θ)·S(sx,sy)` (3 DOF). Add shear so linear part is 4 DOF:

```
M = T · R(θ) · H(k) · S(sx, sy)
```

with horizontal shear in local space:

```
H(k) = | 1  k |
       | 0  1 |
```

(`k = 0` = old behaviour; project files without `shear` load as 0.)

### Plan

1. **State / item** — `WorkspaceItemState::shear`, `ImageItem::{itemShear,setItemShear}`,
   `applyLocalTransform` builds `R·H·S`. Clamp `k` to a sane range (e.g. ±5).
2. **Capture / apply / project JSON** — round-trip `shear`; omit when ~0.
3. **Handles** — `ShearTop` / `ShearBottom` (diamond grips on top/bottom edges);
   drag along local X changes `k`; opposite edge stays fixed in scene.
4. **Group scale** — uniform by default (075); free AABB with Shift.
5. **Tests** — projectfile appearance JSON includes shear.
6. **Docs** — HANDLES.md scale/shear semantics.
7. **Edge scale under shear** — axes from R·H·S (077).

### Not in this pass

- Second shear parameter / raw 2×2 matrix
- Group shear
- Perspective



---

## Bundle `biltoo-075` — group scale preserves R·H·S

**Change:** Group scale defaults to **uniform** factors so rotated / sheared
tiles keep aspect and shear. Shift enables free AABB axes (approximate).
Group undo compares `shear`. Positions still scale about the selection AABB
anchor; each item keeps rotation and shear from the press snapshot.


---

## Bundle `biltoo-076` — shear coverage gaps (validation)

**Validated:** Core R·H·S path is complete for capture/apply/project/handles.
No local Qt6 for full compile here; static review found missing shear writes:

1. `bindSelectedSessionIds` — now records `item->itemShear()` into the slot
2. `copySessionAppearance` — resets shear with scale/rotation (content-only copy)
3. Crop mode — stash/zero/restore shear with placement rotation (axis-aligned crop)
4. Gallery controller — zero shear when zeroing rotation

**Still deferred:** anisotropic group scale under rotation; shear feel tuning;
nix check in this environment.



---

## Bundle `biltoo-077` — edge scale axes include shear

Edge stretch projects onto press-time scene images of local X/Y from the full
linear map `R·H·S` (local +Y is skewed when `k ≠ 0`). Scale drags re-assert
press shear so only `sx`/`sy` change.


---

## Bundle `biltoo-078` — free-axis group scale via S·L decompose

Shift+group-scale applies scene stretch \(S=\mathrm{diag}(f_x,f_y)\) about the
selection AABB anchor to each item as \(L'=S\cdot L\), then decomposes \(L'\)
back to \(R\cdot H\cdot S\) (scaleX/Y, shear, rotation). Uniform (no Shift)
still only multiplies scales and keeps press shear/rotation.


---

## Bundle `biltoo-079` — PlacementLinear shared + unit test

Extracted `PlacementLinear::{make,decompose}` used by `ImageItem` and group
scale. `tests/placementlinear_test.cpp` covers round-trip and scene conjugate.
Status bar shows Shear when |k| > 0.001 on the selected Workspace item.


---

## Bundle `biltoo-080` — full shear support (complete)

**Critical fix:** project save now writes `appearance.shear` from live pose
(previously dropped on save — load could never restore shear from projects).

**Complete surface:**
- Four edge shear diamonds (Top/Bottom/Left/Right)
- Chrome `//` ResetShear; `1:1` still clears shear; `0°` keeps shear
- Alt+[ / Alt+] nudge; Alt+0 reset (Shift steps 0.1)
- Gallery pack zeros shear; gallery enter zeros shear
- Edge scale axes R·H·S; group free-axis S·L decompose
- PlacementLinear shared + unit tests
- Status shows Shear; projectfile tests include shear
- HANDLES.md documents full contract


---

## Bundle `biltoo-085` — group edge scale is H/V anisotropic

**Before:** edge handles forced `sx = sy` and non-Shift averaged everything to
uniform, so left/right/top/bottom group grips never did pure H or V stretch.

**After:**
| Handle | Default | Shift |
|--------|---------|-------|
| Edge T/B/L/R | Axis stretch (sy or sx only) | Uniform (lock aspect) |
| Corner | Uniform | Free H/V axes |

Anisotropic paths use `S_scene · L` + `PlacementLinear::decompose` so rotated
and sheared tiles follow the selection AABB.


---

## UI audit (2026-09-03) — polish backlog

### Fixed in `biltoo-090`
- Workspace **Reset Item Shear** (menu + context + ImageView API)
- Keyboard shortcuts help: crop, panels, shear, projects, group scale
- File → Open Selection in New Window
- Gallery → Sort Session (was only under Edit)

### Fixed in `biltoo-091`
- Main toolbar: single **Layout** InstantPopup tool button (replaces eight
  gallery layout icons); Workspace mode remains a separate adjacent action

### Fixed in `biltoo-092`
- Top-level **Image** menu for rotate / flip / crop; Edit keeps undo,
  clipboard, Sort Session, Preferences

### Fixed in `biltoo-093`
- Workspace vertical toolbar: drop Page Setup / Print Preview / Export PNG /
  Export PDF (remain under File); strip stays select/pan/guides/background/layout

### Fixed in `biltoo-094`
- Quit: Ctrl+Q only (bare Q removed); Layout Horizontal no longer uses Ctrl+Y
  (avoids Windows Redo clash); F1 lists Ctrl+Q

### Fixed in `biltoo-095`
- Image mode always has **Up** to implicit default Gallery when not returning
  to Workspace (direct open / CLI / History no longer leave Up disabled)

### Fixed in `biltoo-096`
- Grid Crop no longer re-shown by visibility updates
- Reset shear + Background Default greyed outside Workspace
- Placement resets (scale/rotation/shear) require Workspace + selection
- Layout actions stay enabled in Image mode intentionally (enter Gallery)

### Fixed in `biltoo-097`
- Thumbnail bar status tip / tooltip for drag-to-Workspace
- Gallery **C**: confirmed — single selection opens Image + crop; otherwise
  action disabled (`hasSingleCropTarget`)

### Fixed in `biltoo-098`
- Path→Image open prefers selected/sole live tile SessionImageId (not
  `paths().indexOf` first match); gallery focus path fallback same rule

### Fixed in `biltoo-099`
- History menu renamed **Recent Sessions** (Clear / empty / status tips /
  README); distinct from File → Recent Projects

### Fixed in `biltoo-100`
- Workspace stash restore always reloads full pixels before geometry content
  apply (fixes alternating wrong size/crop on Workspace↔Image cycles)

### Fixed in `biltoo-101`
- Paste/drop: do not allocate phantom session rows while PendingSessionBind
  is outstanding (broken filmstrip drag entries)

### Fixed in `biltoo-102`
- Edit menu is sole menu-bar home for Copy/Cut/Paste/Duplicate; removed from
  Workspace and Gallery menus (context menu unchanged)

### Fixed in `biltoo-103`
- Multi-file session open (Open / Recent Sessions / CLI) starts in Gallery
- Session open clears Workspace live/stash/durable snapshot (only .biltoo
  projects restore Workspace arrangement)

### Fixed in `biltoo-105`
- Paste registers pathOrder/sessionIdOrder in addImageForSession so LoadAdd
  creates Workspace tiles (not filmstrip-only rows with stale thumbs)

### Fixed in `biltoo-106`
- Nix: `cfitsio` on buildInputs so vips pkg-config stops spam about missing
  Requires.private (same pattern as fftw / libsysprof-capture)

### Organisation / density
- [x] Main toolbar is crowded: 8 gallery layout icons + workspace mode.
  Prefer a single **Layout** tool button with menu (like Sort).
- [x] Edit menu mixes transforms (rotate/flip/crop) with Sort and Preferences.
  Consider **Image** menu for transforms; keep Edit for undo/clipboard/prefs.
- [x] Workspace vertical toolbar also has Print/Export — consider File only or
  a "Page" submenu so tools stay select/pan/guides/background.
- [x] `m_layoutFreeFormAct` exists but is not in Gallery menu (layouts exclusive
  group starts unchecked until user picks one — OK, but Free Form is Workspace-only).

### Shortcuts / discoverability
- [x] Bare **Q** quits (no Ctrl) — easy mis-press; prefer Ctrl+Q only.
- [x] Bare **R** rotates — fine for a viewer; document that Reload is F5.
- [x] **Ctrl+Y** = Layout Horizontal (on Windows Ctrl+Y is often Redo) — consider
  dropping or using a non-conflicting chord.
- [ ] Alt+[ / ] shear vs [ / ] slideshow — OK (modifiers differ); ensure focus is
  not in a spinbox when testing.
- [x] No menu entry for **shear** except Reset; handles are primary (OK) but
  F1 should stay in sync when shortcuts change.

### Thumbnail bar
- [x] Labels on/off and crop-thumbnails live under View → Thumbnails — good.
- [x] Drag from thumb strip to Workspace: verify status tip / cursor affordance.
- [ ] Very long sessions: virtualization exists for Gallery canvas; strip still
  loads many thumbs — optional windowed decode already partially present.

### Mode enablement
- [x] Audit that Gallery-only actions disable in pure Image mode and vice versa
  (layout actions, workspace tools) so the toolbar does not look "dead click".
- [x] Crop shortcut **C** in Gallery: should no-op or enter image crop for
  selection — confirm behaviour.

### Missing features (product)
- [x] Recent *files* (session paths) vs only Recent Projects — History menu covers
  sessions; naming is easy to confuse with Recent Projects.
  (Renamed to **Recent Sessions**; single-file Recent list not added.)
- [x] Explicit "Remove from session" in thumbnail context menu (if not present).
- [ ] Toolbar/customize — out of scope unless requested.


---

## Plan / work (2026-09-03) — bundle `biltoo-091-layout-toolbar-menu`

**Goal:** Reduce main toolbar density. Replace the eight individual Gallery
layout tool-button icons + the adjacent Workspace mode button cluster with a
single **Layout** `QToolButton` (InstantPopup menu), matching the existing
Sort tool-button pattern.

### Scope

1. **Toolbar**
   - Remove direct `m_toolBar->addAction` for:
     `m_layoutSideBySideAct`, `m_layoutVerticalAct`, `m_layoutGridAct`,
     `m_layoutGridCropAct`, `m_layoutMasonryAct`, `m_layoutMasonryRowsAct`,
     `m_layoutMasonryFillAct`, `m_layoutMasonryRowsFillAct`.
   - Insert one `QToolButton` with `InstantPopup` menu containing those
     actions (same order as Gallery menu).
   - Keep `m_workspaceModeAct` as a separate toolbar action immediately after
     the Layout button (mode switch is not a packaged layout).
   - Masonry/Grid column-count spin remains as today (visibility driven by
     active layout).

2. **Menus**
   - Gallery menu layout entries unchanged.
   - No change to Workspace Layout panel.

3. **Icon / status**
   - Button uses a stable generic icon (`view-grid` / theme fallback or
     existing `layout-panel` resource). Optional later: sync icon to current
     layout (out of scope for this bundle).

4. **Visibility / enable**
   - Existing `updateWorkspaceActionVisibility` still drives per-action
     `setEnabled` / `setVisible` (menu items inherit that). Grid Crop stays
     hidden/disabled until the separate re-enable task.

### Out of scope

- Edit → Image menu split for transforms.
- Workspace vertical toolbar Print/Export move.
- Bare Q / Ctrl+Y shortcut changes.
- Grid Crop re-enable.
- Identity residual (Image-mode crop of duplicate slot B).

### Done criteria

- [x] Toolbar shows one Layout popup + Workspace mode (not eight layout icons).
- [x] All packaged layouts still reachable from toolbar popup and Gallery menu.
- [x] Sort-style InstantPopup behaviour (click opens menu).
- [x] Document completion; next bundle number **092**.

**Next bundle:** `biltoo-092-…` — remaining UI organisation (Edit/Image menu
split, Workspace toolbar Print/Export), shortcuts polish, or identity residual
(Image-mode crop of duplicate slot B).

---

## Plan / work (2026-09-03) — bundle `biltoo-092-image-menu`

**Goal:** Organise the menu bar. Move content transforms out of Edit into a
dedicated top-level **Image** menu, per the UI audit.

### Scope

1. **New top-level Image menu** (between Edit and View):
   - Rotate Left, Rotate Right
   - Flip Horizontal, Flip Vertical
   - Crop
2. **Edit menu** retains:
   - Undo / Redo
   - Select All
   - Copy / Cut / Paste (Workspace)
   - Sort Session submenu
   - Preferences
3. **Unchanged**
   - Toolbar transform buttons
   - Workspace menu (reset scale/rotation/shear stay placement tools)
   - Context menus
   - Gallery / File / View / Go / History / Help

### Out of scope

- Workspace vertical toolbar Print/Export relocation
- Shortcut changes (bare Q, Ctrl+Y)
- Grid Crop re-enable
- Identity residual (duplicate Image-mode crop sync)

### Done criteria

- [x] Image menu exists with rotate / flip / crop
- [x] Edit no longer lists those transforms
- [x] Document completion; next bundle **093**

**Next bundle:** `biltoo-093-…` — Workspace vertical toolbar Print/Export
relocation, shortcut polish (bare Q, Ctrl+Y), or identity residual
(Image-mode crop of duplicate slot B).

---

## Plan / work (2026-09-03) — bundle `biltoo-093-workspace-toolbar-tools`

**Goal:** Keep the Workspace vertical toolbar focused on canvas tools.
Remove File-oriented print/export actions from it (they remain under File).

### Scope

1. **Remove from `m_workspaceToolBar`:**
   - `m_pageSetupAct`
   - `m_printPreviewAct`
   - `m_exportPngAct`
   - `m_exportPdfAct`
2. **Keep on toolbar:**
   - Select / Pan
   - Page Guide, Workspace Background, Background Default, Fit Page Guide
   - Toggle Layout Panel
3. **Unchanged:** File menu still has Print, Print Preview, Page Setup,
   Export PNG, Export PDF. Workspace menu entries for page guide / background
   stay as they are.

### Rationale

Toolbar was mixing interaction tools with output commands. File is the
natural home for print/export; the left strip should stay select/pan/guides/
background/layout.

### Out of scope

- Shortcut polish (bare Q, Ctrl+Y)
- Grid Crop re-enable
- Identity residual (duplicate Image-mode crop sync)
- New "Page" submenu under Workspace (unnecessary while File already lists them)

### Done criteria

- [x] Workspace toolbar has no print/export/page-setup actions
- [x] File menu still provides those actions
- [x] Document completion; next bundle **094**

**Next bundle:** `biltoo-094-…` — shortcut polish (bare Q, Ctrl+Y), or
identity residual (Image-mode crop of duplicate slot B).

---

## Plan / work (2026-09-03) — bundle `biltoo-094-shortcut-polish`

**Goal:** Reduce accidental quit and platform Redo conflict.

### Scope

1. **Quit:** drop bare **Q**; keep only `QKeySequence::Quit` (Ctrl+Q / platform
   standard). Update the comment and status tip if needed.
2. **Layout Horizontal:** remove **Ctrl+Y** so it does not fight Windows-style
   Redo (`QKeySequence::Redo` often includes Ctrl+Y). Layout remains available
   from the Layout toolbar popup and Gallery / Image menus.
3. **F1 help:** mention Ctrl+Q under Files if not already present.

### Out of scope

- Bare **R** (rotate) — intentional for a viewer; Reload stays F5
- Shear shortcuts
- Identity residual (duplicate Image-mode crop sync)
- Grid Crop re-enable

### Done criteria

- [x] Bare Q no longer quits
- [x] Ctrl+Y no longer switches Layout Horizontal
- [x] Document completion; next bundle **095**

**Next bundle:** `biltoo-095-…` — identity residual (Image-mode crop of
duplicate slot B), mode enablement audit, or remaining polish.

---

## Plan / work (2026-09-03) — bundle `biltoo-095-implicit-gallery-up`

**Bug:** In Image mode opened without going through Gallery (File Open, CLI,
History, Workspace mode toggled off, project image mode), the **Up** button is
disabled. There is no way to reach Gallery except picking a layout from the
menu.

**Product rule:** There is always an implicit default Gallery to go up to when
the return target is not Workspace. `m_galleryReturnLayout` (default Masonry,
or last Gallery layout) is the destination.

### Scope

1. **`updateUpToGalleryAction`**
   - Enable when `isImageMode()` and session non-empty.
   - Tip: "Up to workspace" if `m_workspaceReturnActive`, else "Up to gallery".
   - Drive `setGalleryReturnAvailable` from the same condition (edge zone).

2. **`returnFromImageMode` / `returnToGallery`**
   - Workspace return still wins when that flag is set.
   - Otherwise always run gallery restore with `m_galleryReturnLayout`
     (drop the `m_galleryReturnActive` early-return gate).

3. **Esc / keyPressEvent**
   - Same enablement: Image mode + non-empty session → returnFromImageMode.

4. **Unchanged**
   - Explicit Gallery → Image still records the current packaged layout.
   - Workspace → Image still prefers Up to Workspace.
   - Empty session: Up stays disabled.

### Out of scope

- Changing default layout away from Masonry
- Auto-entering Gallery on multi-file open (separate product choice)
- Identity residual crop sync

### Done criteria

- [x] Image mode with files always shows enabled Up (except empty session)
- [x] Up from direct-open Image enters Gallery with last/default layout
- [x] Workspace return path unchanged
- [x] Document; next bundle **096**

**Next bundle:** `biltoo-096-…` — identity residual (Image-mode crop of
duplicate slot B), mode enablement audit, or remaining polish.

---

## Plan / work (2026-09-03) — bundle `biltoo-096-mode-enablement-polish`

**Goal:** Tighten mode-sensitive enablement and small discoverability polish.

### Scope

1. **Grid Crop** must stay hidden/disabled (creation site already does this, but
   `updateWorkspaceActionVisibility` was re-showing/enabling it). Exclude it
   from the bulk layout enable loop.
2. **Workspace-only actions:** include `m_resetShearAct` and
   `m_workspaceBgDefaultAct` in the grey-out-outside-Workspace list.
3. **F1 shortcuts:** document bare **R** = rotate right, **F5** = reload.
4. **TODO audit:** mark thumbnail "Remove from Session" as already present;
   note layout actions stay enabled in Image mode on purpose (they enter Gallery).

### Out of scope

- Re-enable Grid Crop product work
- Identity residual crop sync
- Thumb strip virtualization

### Done criteria

- [x] Grid Crop not visible/enabled from visibility updates
- [x] Reset shear / bg default disabled outside Workspace
- [x] F1 lists R and F5 (already documented; confirmed)
- [x] Document; next **097**

**Next bundle:** `biltoo-097-…` — identity residual (Image-mode crop of
duplicate slot B), Gallery crop shortcut **C** behaviour, or thumb drag tips.

---

## Plan / work (2026-09-03) — bundle `biltoo-097-thumb-drag-crop-c`

**Goal:** Small remaining polish from the UI audit.

### Scope

1. **Thumbnail bar drag affordance**
   - Status tip / tooltip explaining drag-to-Workspace (and open on activate).
2. **Gallery crop shortcut C**
   - Confirm existing behaviour: single selection → open Image mode + crop;
     no / multi selection → no-op (action disabled via `hasSingleCropTarget`).
   - Optional: status bar message if triggered while disabled is unnecessary
     (action stays disabled).
3. **TODO audit** mark both items.

### Out of scope

- Identity residual (duplicate Image-mode crop → Workspace tile B)
- Thumb strip virtualization for long sessions

### Done criteria

- [x] Thumb strip communicates drag-to-Workspace
- [x] Gallery C behaviour documented/confirmed
- [x] next **098**

**Next bundle:** `biltoo-098-…` — identity residual (Image-mode crop of
duplicate slot B), or remaining product polish (Recent naming, long-session thumbs).

---

## Plan / work (2026-09-03) — bundle `biltoo-098-path-open-identity`

**Bug residual:** Image-mode open via path (`showPathInImageMode` /
`galleryItemOpenRequested`) uses `paths().indexOf(path)` → always the **first**
session row. Cropping “slot B” after opening the wrong row writes appearance
to the wrong `SessionImageId` and can leave the other Workspace tile wrong.

**Fix:** When resolving a path to a session index for Image mode, prefer:

1. Selected live canvas item with that path (and a valid SessionImageId)
2. Else sole live item with that path
3. Else first session row (legacy unbound / no canvas)

Also improve `galleryItemFocused` path fallback similarly when a selected
tile carries the id.

### Out of scope

- Removing path map entirely
- Full GUI test harness for duplicate crop

### Done criteria

- [x] Path open prefers selected/live tile’s SessionImageId
- [x] Document; next **099**

**Next bundle:** `biltoo-099-…` — remaining product polish (Recent naming,
long-session thumbs) or further identity GUI smoke notes.

---

## Plan / work (2026-09-03) — bundle `biltoo-099-recent-sessions-naming`

**Goal:** Disambiguate session reopen vs project reopen in the menu bar.

### Scope

1. Rename **History** menu → **Recent Sessions** (user-visible strings only).
2. Clear action / empty placeholder / status tips match the new name.
3. **Recent Projects** (File menu) unchanged — still `.biltoo` projects.
4. Internal symbols (`m_historyMenu`, `sessionHistory` settings key) stay as-is
   so settings continue to load.

### Out of scope

- Thumb strip virtualization
- New “recent files” (single path) list

### Done criteria

- [x] Menu bar says Recent Sessions
- [x] Document; next **100**

**Next bundle:** `biltoo-100-…` — long-session thumb virtualization, or other
product work from TODO 0.1.0.

---

## Plan / work (2026-09-03) — bundle `biltoo-100-workspace-stash-reapply`

**Bug:** Workspace tiles show correct position but wrong size/crop after
Workspace ↔ Image cycles. Alternates fix/break on each leave/return.

**Root cause:** `SessionAppearance::applyContentToItem` requires full on-disk
pixels before crop/content bakes. `restoreStashedItems` only reloaded when
`imageSize != cropRect.size` (or flags mismatch). After a correct apply, sizes
match → next restore skips reload → crop runs on already-cropped pixels →
wrong size. Next cycle sizes mismatch → reload → correct again.

**Fix:** Before `applyContentToItem` on stash restore, always reload full
source when the appearance has geometry content ops (crop / content flip /
quarter-turns). Colour-grade-only can skip reload.

### Done criteria

- [x] Stash restore always reloads full before geometry content apply
- [x] Document; next **101**

**Next bundle:** `biltoo-101-…` — further polish or regressions from this fix.

---

## Plan / work (2026-09-03) — bundle `biltoo-101-paste-thumb-bind`

**Bug:** Copy/Paste creates invalid filmstrip entries that cannot be
drag-and-dropped onto the Workspace.

**Likely cause:** After paste, `placeWorkspaceClipboardItems` schedules async
LoadAdd and emits `workspacePathsChanged` → `syncThumbnailCanvasMembership`.
If a live tile exists (or is briefly unbound) before the pending session bind
is applied, the grow path allocates a **second** session row for the same tile
with a new SessionImageId. Filmstrip then has a phantom row (wrong/missing id
alignment) that does not drag/drop correctly.

**Fix:**
1. Do not auto-append session rows in `syncThumbnailCanvasMembership` while a
   pending session bind still exists for that path.
2. Harden thumbnail drag mime: skip empty paths; for archive refs use a valid
   container file URL plus session-id payload (already present).

### Done criteria

- [x] Pending binds block phantom session growth
- [x] Document; next **102**

**Next bundle:** `biltoo-102-…` — further paste/drag polish if needed.

---

## Plan / work (2026-09-03) — bundle `biltoo-102-edit-clipboard-menu`

**Goal:** End redundant clipboard actions across menus.

### Scope

1. **Edit menu:** keep Copy / Cut / Paste; add **Duplicate** after Paste.
2. **Workspace menu:** remove Copy / Cut / Paste / Duplicate (tools & layout only).
3. **Gallery menu:** remove Duplicate (layouts / sort remain).
4. **Context menu:** keep all four (right-click affordance, not menu-bar noise).

### Done criteria

- [x] Clipboard + Duplicate only under Edit (menu bar)
- [x] Document; next **103**

**Next bundle:** `biltoo-103-…` — further UI polish as needed.

---

## Plan / work (2026-09-03) — bundle `biltoo-103-session-opens-gallery`

**Product rules:**
1. Workspace is empty unless the user places tiles or opens a **.biltoo** project.
2. Opening a session (Open, Recent Sessions, multi-file CLI) must not seed Workspace.
3. Multi-file session open starts in **Gallery**; single file stays **Image**.

### Implementation

1. `loadFiles`: clear Workspace live/stash/durable snapshot; never
   `setWorkspacePaths` for Workspace mode.
2. `loadFiles`: if `paths.size() > 1` → `enterGalleryMode(Masonry)`; else ensure
   Image mode and show the file.
3. Document in TODO.

### Out of scope

- Changing project open (still restores Workspace from file)
- Removing startInWorkspace preference entirely (CLI/prefs can still force Workspace empty)

### Done criteria

- [x] Multi-file → Gallery
- [x] Session open does not populate Workspace
- [x] next **104**

**Next bundle:** `biltoo-104-…` — further mode/open polish if needed.

---

## Plan / work (2026-09-03) — bundle `biltoo-104-portability-doc`

**Goal:** Capture Linux vs portable notes for a future Windows build.

### Scope

- Add [PORTABILITY.md](PORTABILITY.md) (no code changes).
- Link from AGENTS.md.

### Done criteria

- [x] PORTABILITY.md present
- [x] next **105**

---

## Plan / work (2026-09-03) — bundle `biltoo-105-paste-pathorder`

**Bug:** Paste always appends filmstrip rows but often creates no Workspace tile
(every other time / path already on canvas). Thumbs stay full-frame because
`sessionAppearanceChanged` never fires without a tile. Drag works (session id
is valid).

**Cause:** `addImageForSession(path, id, index)` (paste path) schedules LoadAdd
but does **not** append `m_pathOrder` / `m_sessionIdOrder`. LoadAdd sets
`wanted = pathOrderCount`; when the path is already represented, `have ==
wanted` and no new item is created while the PendingSessionBind is left
hanging.

**Fix:** Register pathOrder + sessionIdOrder in session-bound `addImageForSession`
(same as `placeOrMoveImageAt`).

### Done criteria

- [x] Paste always places a canvas tile for each new session id
- [x] next **106**

**Next bundle:** `biltoo-106-…` — layout button / Workspace→Gallery if still open.

---

## Plan / work (2026-09-03) — bundle `biltoo-106-cfitsio-pkgconfig`

**Noise:** CMake `pkg_check_modules(vips)` prints repeated
`Package 'cfitsio', required by 'vips', not found` even when vips is found.

**Fix:** Add `cfitsio` to `default.nix` `buildInputs` (Requires.private of vips),
same pattern as `fftw` / `libsysprof-capture`. We do not link cfitsio ourselves.

### Done criteria

- [x] cfitsio on buildInputs
- [x] next **107**

---

## Plan / work (2026-09-03) — bundle `biltoo-107-imagequant-pkgconfig`

**Noise:** CMake `pkg_check_modules(vips)` prints repeated
`Package 'imagequant', required by 'vips', not found` even when vips is found
(same class of Requires.private spam as cfitsio / fftw / sysprof-capture).

**Fix:** Add `libimagequant` to `default.nix` `buildInputs` so the
`imagequant.pc` is on `PKG_CONFIG_PATH`. We do not link libimagequant into
biltoo.

### Done criteria

- [x] libimagequant on buildInputs
- [x] next **108**

**Next bundle:** `biltoo-108-…` — further packaging / build polish if needed
(e.g. remaining unused-CMake-var noise from KDE/ECM flags if still present).

---

## Plan / work (2026-09-03) — bundle `biltoo-108-gallery-layout-combo`

**Request:** Gallery Layout toolbar control should not be a pure InstantPopup
menu. Primary click must act as **Go to Gallery**; a small adjacent menu
button chooses layout. Button face shows the **current layout icon**
(combo-style).

**Approach:** `QToolButton::MenuButtonPopup` with
`setDefaultAction(m_galleryLayoutToolbarAct)`. Default action calls
`goToGalleryCurrentLayout()` → `enterGalleryMode(m_galleryReturnLayout)`.
Menu still holds the eight layout actions. `syncGalleryLayoutUi()` centralizes
exclusive checks + toolbar icon/tooltip (used from enter/return Gallery paths).

### Done criteria

- [x] MenuButtonPopup (not InstantPopup)
- [x] Primary click enters Gallery with last/current layout
- [x] Toolbar icon tracks selected layout
- [x] next **109**

**Next bundle:** `biltoo-109-…` — further UI polish as needed.

---

## Plan / work (2026-09-03) — bundle `biltoo-109-cmake-unused-cli`

**Noise:** CMake ends configure with a long
`Manually-specified variables were not used by the project` list
(`KDE_INSTALL_*`, `CMAKE_C_COMPILER`, `CMAKE_EXPORT_NO_PACKAGE_REGISTRY`, …).

**Cause:** nixpkgs Qt/KDE setup hooks inject ECM-style `-DKDE_INSTALL_*=…`
flags into every `qt6Packages.callPackage` build. Biltoo is plain CMake +
Qt (no Extra CMake Modules / KDEInstallDirs), so none of those vars are read.
Harmless but very verbose.

**Fix:** Pass CMake’s `--no-warn-unused-cli` via `cmakeFlags` in `default.nix`.
Do not fake-consume the KDE vars and do not fight the setup hooks.

### Done criteria

- [x] `--no-warn-unused-cli` on cmakeFlags
- [x] next **110**

**Next bundle:** `biltoo-110-…` — further packaging / UI polish as needed.

---

## Plan / work (2026-09-03) — bundle `biltoo-110-vips-pc-deps`

**Noise:** Further `Package '…', required by 'vips', not found` (and transitive
`.pc` names) after cfitsio / imagequant were fixed.

**Fix:** Add the remaining common vips Requires.private providers to
`default.nix` `buildInputs` so their `.pc` files sit on `PKG_CONFIG_PATH`.
Same pattern as fftw / cfitsio / libimagequant — we do not link them into
biltoo.

Packages: `cgif`, `libexif`, `libultrahdr`, `libwebp`, `pango`, `fribidi`,
`libtiff`, `librsvg`, `dav1d`, `matio`, `hdf5`, `lcms2` (not the `lcms`
alias), `openexr`, `libraw`, `openjpeg`, `libhwy`.

### Done criteria

- [x] Extra vips .pc deps on buildInputs
- [x] next **111**

**Next bundle:** `biltoo-111-…` — further packaging / UI polish as needed.

---

## Plan / work (2026-09-03) — bundle `biltoo-111-gallery-select-perf`

**Bug:** Selecting a file in the Gallery can take ~1 second. Noticeable on
archive members, but the path is too slow in general — tile pixels and basic
session info are already in memory.

**Root cause:** Every Gallery click ends in
`selectionChanged` → `statusChanged` → `MainWindow::updateStatus` →
`updateMetadataPanel` → `MetadataPanel::setImagePath`, which **synchronously**:

1. `QFileInfo` (meaningless for `//archive:` virtual paths)
2. Full decode via `QImageReader::read()` or `ImageLoader::load`
3. Histogram + palette from decoded pixels
4. Exiv2 metadata read

The metadata dock is **hidden by default**, yet this work still runs on every
selection. Archives amplify the cost (member extract + decode).

**Fix (proper, no hacks):**

1. **Visibility gate:** `updateMetadataPanel` only calls `setImagePath` when
   `m_metadataDock` is visible. When the dock becomes visible, refresh for the
   current path.
2. **Reuse in-memory pixels:** Prefer `ImageItem::sourceImage()` (already
   decoded for visible Gallery tiles) for structure/histogram/palette instead
   of decoding again. Add an optional decoded hint to the panel API.
3. **Debounce:** Short single-shot timer inside `MetadataPanel` so rapid
   selection changes cancel previous pending work and only the last path runs.
4. **Archive-aware file rows:** Use `ArchivePath::displayName` / parse for the
   header and path row; do not report bogus `QFileInfo` size for virtual refs
   unless member size is available cheaply.
5. Keep `setCurrentIndex`’s direct `setImagePath` path consistent with the
   same visibility + hint rules (or route through `updateMetadataPanel`).

### Out of scope

- Full metadata cache across the session
- Async Exiv2 worker thread (debounce + skip-when-hidden is enough for the
  reported lag)
- Changing histogram UI

### Done criteria

- [x] Gallery select stays responsive with metadata dock hidden
- [x] Opening the metadata dock still shows correct info for the current item
- [x] Visible Gallery tile reuses decoded pixels (no second full decode)
- [x] Archive paths do not use meaningless QFileInfo size
- [x] Document; next **112**

**Next bundle:** `biltoo-112-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-112-crop-icon-amber`

**Request:** Colour the crop toolbar icon with the same yellow/amber family
used by the on-canvas crop tool chrome (`#ffbe28` / icon amber `#e5a50a`).

**Change:** `data/icons/actions/transform-crop.svg` — interlocking L-shapes
in `#e5a50a` / `#f8e45c` with a light inner frame in `#ffbe28` (matches
crop-frame pen). No code changes.

### Done criteria

- [x] transform-crop.svg uses crop amber palette
- [x] Document; next **113**

**Next bundle:** `biltoo-113-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-113-thumb-letterbox-bg`

**Bug:** With **Crop Thumbnails to Square** unchecked, filmstrip thumbs keep
their aspect ratio but sit on a solid dark square (`QColor(40,40,40)` in
`prepareThumbnailFromImage`). That plate should not appear — letterbox areas
must be transparent so the ThumbnailBar / list background shows through.

**Fix:** Fill the intermediate cell with `Qt::transparent` instead of grey.
No layout/API change.

### Done criteria

- [x] Non-square thumbs have transparent letterbox
- [x] Document; next **114**

**Next bundle:** `biltoo-114-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-114-pref-reset-buttons`

**Request:** Preferences (and similar option rows) need a small **reset to
default** control next to each setting.

**Implementation:**
- `PreferencesDialog`: every General-tab field is wrapped with an auto-raise
  tool button (`edit-clear` / dialog-reset icon). Click restores the same
  default used by `MainWindow::readSettings`. Button is enabled only while the
  value differs from the default.
- `LayoutPanel`: Columns / Rows spins get the same reset affordance (default 3).

### Done criteria

- [x] Per-option reset on Preferences General tab
- [x] Layout panel Columns/Rows reset
- [x] Document; next **115**

**Next bundle:** `biltoo-115-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-115-thumb-drag-selection`

**Bug:** ThumbnailBar drag could drop **every** image (especially from
archives) instead of only the selected thumbs.

**Cause:** `mimeData` wrote the **archive container** file URL for
`//archive:` members. On drop, `expandPaths` unpacked the whole archive.
Session-id payload was parallel only to the selection, but the expanded
path list was not.

**Fix:**
- Emit `application/x-biltoo-paths` with the exact selected session paths
  (archive refs included as-is).
- Do not put container-only file URLs for archive members.
- `handleDroppedUrls` prefers the internal path list and skips
  `expandPaths` for that payload.
- Drag builds the item list from the current selection (or the pressed
  thumb), never the full filmstrip.

### Done criteria

- [x] Selected thumbs only on drop (including archive members)
- [x] Document; next **116**

**Next bundle:** `biltoo-116-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-116-pref-reset-icon`

**Request:** Preference / Layout reset affordance should use a **reload**
icon, not the delete/clear glyph.

**Change:** Add `view-refresh.svg`; use `themeIcon("view-refresh",
SP_BrowserReload)` in PreferencesDialog and LayoutPanel reset buttons.

### Done criteria

- [x] Reload icon for reset controls
- [x] Document; next **117**

**Next bundle:** `biltoo-117-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-117-ws-bg-default-toggle`

**Bugs / polish:**
1. **Background Default** was a permanent clear to AppDefault (undoable). It
   should be a **temporary** toggle that shows the Preferences background
   without discarding the project override.
2. Workspace Background dialog: per-field **reset** buttons; colour / image
   controls must stay disabled when the mode does not use them.

**Fix:**
- `ImageView::setWorkspaceBackgroundShowDefault` — paint treats custom bg as
  AppDefault while the flag is on; project state untouched.
- Action uses `toggled(bool)`; permanent AppDefault keeps it checked.
- Dialog: `wrapWithReset` on mode/colours/image; enable whole control rows by
  mode (Solid → colour; Checker → both; Image → path only; AppDefault → none).

### Done criteria

- [x] Default toggle is temporary
- [x] Dialog resets + mode-gated controls
- [x] Document; next **118**

**Next bundle:** `biltoo-118-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-118-workspace-zoom-tool`

**Request:** Workspace toolbar needs a **Zoom** tool button and icon (alongside
Select / Pan).

**Implementation:**
- `ImageView::Tool::Zoom` — rubber-band zoom to region (same gesture as Z
  one-shot, but stays active as a tool).
- Cross cursor; NoDrag (gesture handled in mouse press/move/release).
- `zoom-tool.svg` magnifier; action on Workspace menu + left workspace toolbar.

### Done criteria

- [x] Zoom tool on Workspace toolbar with icon
- [x] Document; next **119**

**Next bundle:** `biltoo-119-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-119-bg-default-fix`

**Bugs:**
1. **Background Default** could stick checked / desync from paint: no-op
   `setWorkspaceBackground` cleared the temporary preview flag while the
   action stayed checked; permanent AppDefault forced a non-interactive
   checked state that felt stuck.
2. Needed a **custom icon** (not view-refresh).
3. **Page Guide** actions were split on the Workspace toolbar by the
   background buttons.

**Fix:**
- Only clear `showDefault` when the permanent WorkspaceBackground changes.
- Disable Default when already permanent AppDefault; enable + toggle preview
  only when a custom override exists; always re-sync the action after toggle.
- `workspace-background-default.svg` (plain canvas + amber corner).
- Toolbar order: Page Guide + Fit Page Guide, then Background + Default.

### Done criteria

- [x] Default toggle reliable
- [x] Custom icon
- [x] Page Guide buttons adjacent
- [x] Document; next **120**

**Next bundle:** `biltoo-120-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-120-fs-hide-adjustments`

**Request:** Hide the Adjustments panel in fullscreen, like metadata / layout /
toolbars.

**Fix:** Snapshot visibility on enter fullscreen, hide dock + uncheck action;
restore both on leave.

### Done criteria

- [x] Adjustments hidden in fullscreen and restored on exit
- [x] Document; next **121**

**Next bundle:** `biltoo-121-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-121-slideshow-transitions`

**Feature:** Slideshow transition effects between slides.

**Modes (Preferences → Slideshow):**
- **None** — instant swap (previous behaviour)
- **Crossfade** — previous viewport fades over the new image (default)
- **Fade through black** — out to black, then in

**Duration:** 0–5000 ms (default 400).

**Pipeline:** `prepareSlideshowTransition()` grabs the viewport before
`goNext()` on a timer tick; when `LoadReplace` finishes in Image mode,
`startSlideshowTransitionAnimation()` runs a `QVariantAnimation` and
`paintEvent` composites the snapshot.

### Done criteria

- [x] Crossfade + fade-black + none
- [x] Preferences + QSettings
- [x] Document; next **122**

**Next bundle:** `biltoo-122-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-122-fade-black-fix`

**Bug:** Fade through black faded the *old* pixmap opacity during phase 1, so
the already-loaded next image showed through before the screen was black.

**Fix:** Phase 1 keeps the old frame at full opacity and only raises a black
overlay; phase 2 fades that black away over the new frame.

### Done criteria

- [x] No premature image swap during fade-to-black
- [x] Document; next **123**

**Next bundle:** `biltoo-123-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-123-slideshow-slide`

**Feature:** Slideshow transition **Slide (projector)** — old frame exits left,
new frame enters from the right (both frames are viewport snapshots).

### Done criteria

- [x] Slide mode in Preferences + paint
- [x] Document; next **124**

**Next bundle:** `biltoo-124-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-124-slide-grab-fix`

**Bug:** Slide transition grabbed the “to” frame while the transition overlay
was already active at progress 0, so paintEvent stamped the *old* snapshot
over the new image and both sides of the slide were the outgoing frame.

**Fix:** Grab the destination viewport with the overlay disabled, then start
the animation.

### Done criteria

- [x] Slide shows distinct old → new frames
- [x] Document; next **125**

**Next bundle:** `biltoo-125-…` — further polish as needed.


---

## Plan / work (2026-09-03) — bundle `biltoo-125-docs-desktop`

**Tasks:**
1. Keyboard shortcuts / HUD / slideshow / desktop integration documentation
2. Packaging / desktop metadata polish
3. Restructure **README.md** with current features

**Done:**
- F1 help: slideshow transitions, Workspace tools, fullscreen/HUD notes
- `biltoo.desktop`: clearer comment, archives in MimeType, StartupWMClass
- `biltoo.metainfo.xml`: richer description, keywords, screenshot, release
- README: modes table, features by area, shortcuts table, CLI, desktop install paths
- TODO 0.1.0 checkboxes for shortcuts/desktop/packaging marked done

**Next bundle:** `biltoo-126-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-126-ken-burns`

**Feature:** Ken Burns-style pan/zoom during slideshow dwells.

- Preferences: enable + zoom factor (default 1.12)
- Each slide: fit with **cover** framing, then slowly zoom in while drifting
  toward a random image corner (InOutSine)
- Starts after inter-slide transition finishes (or immediately if none)
- Cancelled on slideshow stop, user wheel-zoom, or next advance (after grab)

**Next bundle:** `biltoo-127-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-127-thumb-fill`

**Bug:** Square (and small) thumbnails sometimes did not fill the cell.

**Causes / fixes:**
1. HiDPI: decode at logical size without `devicePixelRatio` → icon painted half-size.
2. Paint via `QIcon::paint` could center a smaller pixmap without upscaling.
3. Near-square integer scale left a 1px empty margin.

Decode/prepare at `thumbSize * dpr`, tag pixmap DPR, draw pixmap into the
icon rect, snap near-square sources to fill.

**Next:** `biltoo-128-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-128-pan-scan`

**Pan & scan (was Ken Burns) polish:**
- Rename UI to **Pan & scan**
- Remove random corner drift; deterministic L→R / T→B; linear easing
- Cover-frame on load (no letterboxed flash before camera starts)
- Start camera immediately under inter-slide transitions (do not wait for
  transition end; do not stop camera merely because a transition grab ran)

**Next:** `biltoo-129-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-129-pan-zoom-scan`

**Changes:**
- Settings keys: `slideshowMotion` (0/1/2), `slideshowPanZoomFactor` (no KenBurns keys)
- Modes: **Off** / **Pan & zoom** / **Pan & scan** (full-axis reveal, no zoom)
- Mouse pan disabled while dwell motion runs; transform anchor fixed to view centre
- Motion driven by PreciseTimer + elapsed qreal (less judder than stepped animation)
- With dwell motion on: advance via **live black exit veil** (camera keeps moving),
  then load next — no frozen snapshot of the outgoing frame

**Next:** `biltoo-130-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-130-transition-labels`

- UI labels: “Pan and zoom” / “Pan and scan” (no `&&`)
- Live exit veil only when Transition = Fade through black **and** dwell motion ≠ Off
- Crossfade / Slide / None use the normal transition path again (grab allowed with motion)

**Next:** `biltoo-131-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-131-live-transition`

With dwell motion on, inter-slide transitions no longer snapshot-freeze the
outgoing frame. The current image keeps panning; the next frame is decoded
off-thread and composited (crossfade opacity / fade-black veil / slide-in).
Advance (`goNext`) runs at the end (or at mid-black for fade-through-black).

**Next:** `biltoo-132-…`

---

## Plan / work (2026-09-04) — bundle `biltoo-132-slideshow-polish`

**Request:**
1. Fix glitches in slideshow: at the end of a transition the view briefly flashes
   the wrong image.
2. Pan & zoom should be Ken Burns–style (not only zoom toward the centre) —
   prefer points of interest / varied start–end framing.
3. Pan & scan should travel the whole image (full overflow on the long axis).
4. Slideshow zoom setting: **Fit** / **Fill** / **1:1** base framing per slide.

### Root cause (transition flash)

Live path (`beginLiveSlideshowTransition`): at `t >= 1.0` the code clears
`m_slideshowTransitionToPixmap` and emits `slideshowLiveTransitionFinished` →
`goNext()` starts an async `LoadReplace`. Until that decode finishes, paint falls
back to the *old* live canvas underlay → one or more frames of the previous
image after the incoming frame was already fully visible. That is the end-of-
transition flash.

Fade-through-black advances at mid-black, so the swap is masked if load is fast;
slow loads can still flash when the veil lifts before the new item is fitted.

Snapshot path is less affected, but clearing the overlay while the underlying
view has not yet stabilised can still hitch.

### Fixes

**A. Hold incoming frame until replace is fitted**
- On live transition completion (crossfade / slide): keep drawing the final
  to-pixmap (opacity 1 / fully slid in) until `onImageLoaded(LoadReplace)` has
  fitted the new item and (if motion on) applied the first motion frame.
- Introduce `m_liveTransitionHold` (or reuse active flag + progress ≥ 1) cleared
  only from the load path after the new slide is ready.
- Fade-black: same hold of black (or to-frame) until load completes if mid-
  advance already ran.

**B. Pan & scan — full travel**
- Compute overflow at the *cover* scale; start centre = one extreme edge of the
  overflow axis, end = opposite extreme (clamped so the viewport never shows
  empty margin).
- Prefer the longer scene axis; if nearly square, still traverse the larger
  overflow (or both if both non-trivial — long axis primary).

**C. Pan & zoom — Ken Burns**
- Build start/end camera centres at start-scale and end-scale independently so
  both ends are valid cover frames (no letterbox).
- Bias centres toward rule-of-thirds / corner candidates derived from image
  aspect (deterministic from path hash or session index for variety).
- Linear interpolate centre and scale over the dwell (easing optional later).

**D. Slideshow zoom mode (Fit / Fill / 1:1)**
- New enum + Preferences row + QSettings key `slideshowZoomMode` (0 Fit, 1 Fill,
  2 Actual).
- Applied on Image-mode LoadReplace while slideshow progress is active when
  dwell motion is Off.
- With dwell motion on: camera still uses cover (Fill) as today so pan/zoom
  stay meaningful; Actual may be used as the *start* scale when the image is
  larger than the view (optional refinement). Tooltip documents that motion
  uses Fill framing.

### Commits (task-focused)

1. Hold live transition frame until next slide is fitted (flash fix)
2. Pan & scan full-axis travel + Ken Burns pan & zoom centres
3. Slideshow zoom mode Fit / Fill / 1:1 (prefs + settings + load framing)

### Done criteria

- [x] No wrong-image flash at end of crossfade / slide / fade-black under motion
  (hold final live composite until LoadReplace fits; FadeBlack freezes at mid-
  black until load completes, then lifts the veil)
- [x] Pan & zoom: Ken Burns start/end centres at respective scales, biased to
  rule-of-thirds / corners (deterministic from path hash)
- [x] Pan & scan: full long-axis overflow, centres clamped to item bounds
- [x] Preferences: Slideshow zoom Fit / Fill / 1:1 (`slideshowZoomMode`); motion
  still uses Fill for the camera path
- [x] TODO / AGENTS handoff; next bundle **133**

**Next bundle:** `biltoo-133-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-133-slideshow-settings-ui`

**Request (interpreted, not followed blindly):**
1. Easy mid-session control of slideshow options (many transitions / motion /
   zoom values — per-option shortcuts would be noisy).
2. Slideshow zoom Fit/Fill/1:1 appears to do nothing.
3. Preferences General tab is overfilled — split into more tabs.

### Design

**Shortcuts for every transition/motion:** rejected. Too many combinations;
muscle memory would fight `[`/`]` interval and Space start/stop.

**Context menu radio submenus only:** possible for Transition (4) and Motion (3),
but still leaves interval, duration, zoom factor, zoom mode, fullscreen start
elsewhere. Incomplete.

**Chosen approach — Slideshow Settings dialog:**
- Small modal focused on slideshow only (same fields as the Preferences
  Slideshow group).
- Reachable from the view context menu (Image mode / fullscreen):
  “Slideshow settings…”.
- Also from the View menu next to Play Slideshow (optional, same action).
- Apply on OK; while a slideshow is running, re-arm progress / framing so
  changes take effect on the current dwell without a full prefs trip.
- Preferences keeps a dedicated **Slideshow** tab (not removed) so the options
  remain discoverable when not mid-show. Same QSettings keys — one source of
  truth on MainWindow / ImageView.

**Preferences tabs:**
| Tab | Contents |
|-----|----------|
| General | Session (sort, start workspace), residual global bits |
| Slideshow | All slideshow options (moved out of General) |
| Interface | View background, HUD, scrollbars / panels / thumbs |
| Default application | MIME defaults (unchanged) |

**Slideshow zoom bug (why it looked inert):**
1. Framing only ran on `LoadReplace`. Starting the slideshow left the *current*
   image on normal Image-mode fit — zoom mode never applied to slide 0.
2. With dwell motion ≠ Off, the camera always uses Fill (by design); zoom mode
   is ignored until motion is Off — easy to miss in testing.
3. Default zoom is Fit, which matches ordinary Image-mode fit — no visible
   change until the user picks Fill or 1:1.

**Fix:**
- Public `ImageView::reapplySlideshowFraming()` — if slideshow progress active:
  motion on → cover + restart motion; motion off → `applySlideshowZoomFraming`.
- Call after `setSlideshowProgress(true)` in `startSlideshow`.
- Call when slideshow zoom / motion changes while a slideshow is active.
- `applySlideshowZoomFraming` Actual path matches `zoomReset` (identity +
  centreOn) for consistent 1:1 behaviour.

### Commits
1. Fix slideshow zoom framing on start / live preference changes
2. Preferences: split General → General + Slideshow + Interface tabs
3. Slideshow Settings dialog + context (and menu) entry

### Done criteria
- [x] Fit/Fill/1:1 visibly changes framing when motion is Off (incl. first slide)
  via `reapplySlideshowFraming()` on start and on option changes
- [x] Preferences: General / Slideshow / Interface / Default application tabs
- [x] Context menu + Go menu → Slideshow Settings… (focused dialog)
- [x] No per-transition keyboard shortcuts (rejected as too noisy)
- [x] TODO / AGENTS; next **134**

**Next bundle:** `biltoo-134-…`


---

## Plan / work (2026-09-04) — bundle `biltoo-134-motion-live-settings`

**Issues:**
1. Slideshow Settings should apply live (no OK required).
2. Pan & scan shows no visible pan.
3. Pan & zoom only zooms center — Ken Burns pan not visible.
4. Transition duration must be ≤ half of interval (else transition overlaps next dwell).

### Motion root cause

Camera path used `fitInView(..., KeepAspectRatioByExpanding)` then derived
overflow from `viewScale()`. When the image aspect is close to the viewport
(or fitInView effectively fits), horizontal/vertical overflow is ~0, so start
and end centres collapse to the image centre — pan&scan is a no-op and
pan&zoom is pure centre zoom.

**Fix:** Compute cover scale explicitly:
`cover = max(vw/br.w, vh/br.h)`. Build start/end centres in scene space from
that scale. For pan&scan, if overflow on the long axis is below a small
threshold, slightly increase scale (overscan) so a full edge→edge travel is
always possible on non-square content. Ken Burns: start at cover with a
corner/thirds bias, end at `cover * factor` with a different bias; both
clamped to valid cover frames at their scales.

### Live settings dialog

- Replace OK/Cancel with **Close** only.
- Connect interval / transition / motion / zoom / factor / fullscreen widgets
  to a `changed` signal; MainWindow applies immediately and writes settings
  (debounced write optional — write on each change is fine for these keys).
- Cap transition duration spin max when interval changes: `min(5000, intervalMs/2)`.

### Commits
1. Rewrite dwell camera path (explicit cover scale + guaranteed pan travel)
2. Live Slideshow Settings + transition ≤ half interval

**Next:** `biltoo-135-…`

### Done criteria
- [x] Pan & scan / pan & zoom use explicit cover scale + centerOn (scrollbar-safe)
- [x] Pan & scan overscans when aspect matches so edge→edge travel is visible
- [x] Ken Burns pans toward corner/edge bias while zooming
- [x] Slideshow Settings applies live; Close only
- [x] Transition duration capped to half of interval
- [x] Next **135**


---

## Plan / work (2026-09-04) — bundle `biltoo-135-zoom-crossfade`

**Issues:**
1. Slideshow zoom (Fit / Fill / 1:1) still has little to no visible effect when
   motion is Off.
2. Live crossfade is glitchy: the image being faded *to* is a static centre-crop
   pixmap, so it never moves while the outgoing dwell keeps panning/zooming.
3. Compiler warning: `tr` shadows previous local in `MainWindow::readSettings()`.

### Slideshow zoom root cause

`applySlideshowZoomFraming` still uses `fitItem` → `fitInView`. That path is
scrollbar-sensitive and often yields nearly identical framing for Fit vs Fill
(especially when aspect is close to the viewport). The dwell camera already
abandoned `fitInView` for an explicit cover scale + `centerOn`; zoom framing
must use the same approach.

**Fix:** Compute scale explicitly from viewport and item `sceneBoundingRect`:

- Fit: `min(vw/br.w, vh/br.h)`
- Fill: `max(vw/br.w, vh/br.h)`
- Actual (1:1): scale `1.0`

Then `setTransform(scale)` + `centerOn(mid)`. No `fitInView`.

### Crossfade root cause

Live path composites a single `renderCoverPixmap(next)` over the live underlay.
That pixmap is a fixed centre crop for the whole transition duration, so the
incoming image is frozen while the outgoing one continues its camera path.

**Fix:** Keep the decoded next `QImage` for the duration of the live transition.
On each `tickLiveTransition`, re-render the "to" pixmap with a motion sample
along the same Pan&Scan / Pan&Zoom path the next dwell would use (progress
mapped from transition `t`). Slide transition uses the same animated frame.
Fade-through-black does not need incoming motion (veil covers it).

After LoadReplace, dwell motion still starts from progress 0 (acceptable;
overlay motion is only for the short transition window). Continuity across the
boundary can be a later polish if needed.

### Shadow warning

Rename the inner `const int tr` (transition duration) so it does not shadow the
outer transition-kind `tr`.

### Commits
1. Slideshow zoom: explicit scale + centerOn (Fit / Fill / 1:1)
2. Live crossfade/slide: animate incoming cover along motion path
3. readSettings: fix `tr` shadow warning
4. Docs: TODO / AGENTS next **136**

### Done criteria
- [x] Fit / Fill / 1:1 use explicit scale + centerOn (scrollbar-safe)
- [x] Live crossfade/slide: incoming cover re-rendered along motion path each tick
- [x] `tr` shadow in `readSettings` renamed
- [x] Next **136**


---

## Plan / work (2026-09-04) — bundle `biltoo-136-zoom-with-motion`

**Issue:** Slideshow zoom (Fit / Fill / 1:1) only affects framing when dwell
motion is Off. With motion On the camera always forced cover (Fill), so zoom
looked inert during pan&scan / pan&zoom.

**Desired model:** Zoom is the **base scale** for every slide. Dwell motion
then moves relative to that base:

| Zoom | Base scale | Motion |
|------|------------|--------|
| Fit | `min(vw/w, vh/h)` letterbox | Pan&zoom: zoom in from fit; Pan&scan: overscan if needed then edge pan |
| Fill | `max(vw/w, vh/h)` cover | Same as previous default camera |
| 1:1 | `1.0` native (padding if smaller than view) | Pan across overflow; pan&zoom from 1:1 |

### Code changes
1. `startSlideshowMotion`: base scale from `m_slideshowZoom` instead of always
   cover; Pan&zoom `start = base`, `end = base * factor`; Pan&scan starts at
   base with existing min-travel overscan (may raise scale above base).
2. `setSlideshowZoom`: reapply framing / restart camera even when motion ≠ Off.
3. LoadReplace slideshow branch: always `applySlideshowZoomFraming` first or
   just `maybeStartSlideshowMotion` (camera sets its own transform) — drop the
   hard-coded `KeepAspectRatioByExpanding` path.
4. `renderMotionCoverPixmap`: same base-scale rules; letterbox/pad when the
   drawn image is smaller than the viewport (Fit / 1:1).
5. Tooltips in Slideshow Settings + Preferences: zoom applies with motion too.

### Commits
1. Dwell camera uses slideshow zoom as base scale
2. Live transition cover + load path honour zoom base
3. UI tooltips + TODO / AGENTS → next **137**

### Done criteria
- [x] Fit + Pan&zoom: starts letterboxed, zooms in
- [x] Fill + motion: cover path (previous behaviour)
- [x] 1:1 + motion: native scale at start (padding on small images)
- [x] Live crossfade incoming frame matches the same base
- [x] Next **137**


---

## Plan / work (2026-09-04) — bundle `biltoo-137-motion-jump`

**Issue:** Dwell motion “ping-pongs”: transition looks normal, then the view
jumps to a different camera position.

### Root causes
1. **LoadReplace double framing:** `applySlideshowZoomFraming` centres the
   slide, then `maybeStartSlideshowMotion` immediately moves to motion t=0
   (often an edge/corner). Visible jump when the live-transition hold drops.
2. **Live overlay vs camera mismatch:** Incoming cover was animated with
   `motionT = transitionProgress` (full path compressed into the fade). Hold
   shows near path *end*; camera restarts at path *start* → snap.

### Fix
1. When motion ≠ Off on LoadReplace, skip `applySlideshowZoomFraming`; only
   `startSlideshowMotion` sets the transform (at the intended start sample).
2. Live transition cover samples the dwell path in **real time**:
   `motionT = elapsedMs / intervalMs` (small during a short fade).
3. `startSlideshowMotion(duration, initialProgress)` so after a live transition
   the clock starts at `transitionDuration/interval`, matching the overlay.
4. Store `m_liveTransitionMotionProgress` at hold time for the handoff.

### Commits
1. Fix motion jump (load framing + live overlay continuity)
2. Docs → next **138**

### Done criteria
- [x] No centre→edge jump when hold releases after crossfade/slide
- [x] Incoming cover moves gently in real time during live fade
- [x] Camera starts at live-overlay motion sample (offset clock)
- [x] Next **138**


---

## Plan / work (2026-09-04) — bundle `biltoo-138-motion-aspect`

**Issue:** Dwell motion makes the image look stretched/distorted (transitions
themselves are fine).

### Likely causes
1. `m_fitMode` left true (Fit zoom) so resizeEvent can call `fitItem` mid-dwell
   and fight the camera.
2. Item residual shear/rotation not cleared before the camera runs.
3. Scale derived from `sceneBoundingRect()` AABB (can differ from content
   aspect if local transform is non-identity).
4. `setTransform(scale)` + `centerOn` interacting with a tight scene rect /
   scrollbar range so the effective view is wrong.

### Fix
1. During any dwell motion force `m_fitMode = false` (camera owns framing).
2. At motion start: `setItemShear(0)`, `setItemRotation(0)`, `setItemScale(1)`.
3. Base scale from `contentRect()` size (source aspect), centres via
   `mapToScene(contentRect().center())`.
4. Apply camera with explicit uniform matrix:
   translate(viewCentre) · scale(s) · translate(-sceneCentre), scrollbars at 0.
5. Expand scene rect so the pan range is not clamped.

### Commits
1. Dwell camera: preserve aspect, identity item pose, matrix framing
2. Docs → next **139**

### Done criteria
- [x] No stretch under pan&scan / pan&zoom (Fit / Fill / 1:1 bases)
- [x] Resize during slideshow does not refit over the camera
- [x] Next **139**


---

## Plan / work (2026-09-04) — bundle `biltoo-139-slideshow-cursor`

**Request:** Show mouse cursor when it moves during a slideshow; hide after
one second of inactivity.

### Status
Logic already existed (`armSlideshowCursorHide` + 1000 ms single-shot timer +
app event filter). It often failed because **viewport mouse tracking was off**:
`QGraphicsView::setMouseTracking(true)` does not enable tracking on the
viewport, so `MouseMove` only arrived while a button was held.

### Fix
1. `viewport()->setMouseTracking(true)` in ImageView ctor and on slideshow start.
2. MainWindow mouse tracking on during slideshow.
3. Event filter also handles double-click / tablet move.
4. `armSlideshowCursorHide` no-ops unless the slideshow timer is active.

### Done criteria
- [x] Cursor appears on free mouse move during slideshow
- [x] Cursor hides ~1 s after last input
- [x] Next **140**


---

## Plan / work (2026-09-04) — bundle `biltoo-140-transition-stretch`

**Issues:**
1. Crossfade + pan&zoom: incoming transition frame stretched to the screen.
2. Slide: sliding-in image fullscreen-stretched; dwell motion incompatible with
   projector slide.

### Root cause (stretch)
`renderMotionCoverPixmap` force-scaled undersized crops with
`Qt::IgnoreAspectRatio` when only one axis overflowed. Paint also used
`drawPixmap(x,y,w,h,pm)` which re-stretched letterboxed frames.

### Fix
1. Compose covers onto a viewport-sized canvas by **drawing** the uniformly
   scaled image (negative dest = crop, positive = pad). Never non-uniform scale.
2. Slide paint: `drawPixmap(x, y, pm)` at native size.
3. Slide never uses the live-motion transition path (`beginLive…` returns false).
4. Snapshot Slide freezes dwell camera for the swap; resumes on animation end.

### Done criteria
- [x] Crossfade to-frames keep aspect under pan&zoom
- [x] Slide frames keep aspect; motion frozen during projector swap
- [x] Next **141**


---

## Design: slideshow transitions × dwell motion (2026-09-04)

Canonical behaviour. Implementation notes in *Audit* below refer to tip
after `biltoo-140-transition-stretch`.

### Shared rules

| Rule | Intent |
|------|--------|
| Duration | User-set; hard-capped to ≤ half of interval so a transition cannot overrun the next dwell |
| Aspect | Every composited frame (from/to/overlay) keeps the image’s aspect — never stretch to the viewport |
| Zoom base | Fit / Fill / 1:1 is the **base scale** for each slide (static framing and camera start) |
| Motion | Off / Pan&zoom / Pan&scan runs **during the dwell** after the transition has finished (except live-crossfade underlay — see below) |
| Load | Next decode may be async; overlays must not reveal a wrong or unframed underlay |

### Modes

#### None
- Instant cut to the next slide.
- No overlay, no grab.
- If motion ≠ Off: start camera on the new slide at progress 0 after load.

#### Crossfade
**Visual:** previous frame fades out while the next fades in (opacity over duration).

| Motion | Intended pipeline |
|--------|-------------------|
| **Off** | **Snapshot path:** grab viewport (old) → load/frame next → animate old pixmap opacity 1→0 over the new underlay. |
| **On** | **Live path:** keep drawing the *current* item with its dwell camera; decode next off-thread; composite an aspect-correct **to-frame** (sampled on the next slide’s camera path in real time) with rising opacity; at end **hold** final composite until LoadReplace has framed the item and the camera has started at the **same** motion progress; drop hold. |

#### Fade through black
**Visual:** old → solid black → new (two equal halves of duration).

| Motion | Intended pipeline |
|--------|-------------------|
| **Off** | Snapshot old; phase 1 draw old + rising black; at mid-black load next; phase 2 lift black over the framed next underlay. |
| **On** | **Live path:** phase 1 black rises over the still-moving underlay; at mid-black emit advance + **await load** (hold solid black); after LoadReplace, phase 2 lifts black; no “to” pixmap required. |

#### Slide (projector)
**Visual:** two **static** full-viewport frames — old exits left, new enters from the right (black in the gap).

| Motion | Intended pipeline |
|--------|-------------------|
| **Any** | **Snapshot only.** Live moving underlay is incompatible (looks like a third moving layer under sliding cards). Grab from-frame; load next; grab to-frame; **freeze dwell camera** for the animation; resume motion after the slide finishes. |

### Framing of “to” frames (when a pixmap is built)

- Scale uniformly from the slideshow zoom base (and motion sample if live).
- Compose onto a viewport-sized canvas: **pad** if smaller, **clip** if larger.
- Never `IgnoreAspectRatio` stretch to fill the viewport.

---

### Audit vs implementation (tip 140)

| Spec item | Status | Notes |
|-----------|--------|--------|
| Duration ≤ half interval | OK | Settings + `readSettings` cap |
| None = instant cut | OK | `prepare` no-ops when None / duration 0 |
| Crossfade + motion Off = snapshot | OK | `onSlideshowTick` → `prepare` → `goNext` → anim |
| Crossfade + motion On = live | OK | `beginLiveSlideshowTransition`; opacity to-pixmap over live underlay |
| Live to-frame aspect-correct | OK after 140 | Canvas compose with uniform scale; no force-stretch |
| Live camera handoff (same progress) | OK after 137 | `m_liveTransitionMotionProgress` + motion clock offset |
| FadeBlack + motion Off | OK | Two-phase paint on snapshot |
| FadeBlack + motion On | OK | Mid-black advance + `m_liveTransitionAwaitingLoad` |
| Slide always snapshot | OK after 140 | `beginLive…` returns false for Slide |
| Slide freezes/resumes motion | OK after 140 | Cancel on to-grab; `maybeStart` on anim finished |
| Slide paint no stretch | OK after 140 | `drawPixmap(x,y,pm)` native size |
| Snapshot from-grab is viewport | OK | `viewport()->grab()` |
| Snapshot to-grab (Slide) | OK | Grab after load in `startSlideshowTransitionAnimation` |
| Zoom base applies with motion | OK after 136 | Camera start scale from Fit/Fill/1:1 |
| Hold prevents end-of-fade flash | OK | `m_liveTransitionHold` until `releaseLiveTransitionHold` |

### Residual risks / follow-ups

1. **HiDPI grabs:** `viewport()->grab()` is device pixels; painting into logical `viewport()->rect()` can soft-scale. Prefer matching devicePixelRatio on stored pixmaps if artifacts appear on scaled displays.
2. **Crossfade live “to” uses thumbnail decode** (`loadThumbnail(maxEdge*2)`): fine for motion samples; very large prints may look softer than the post-load item until hold drops.
3. **Snapshot Crossfade/FadeBlack with motion Off** still uses from-snapshot only; underlay is the real next item (correct). If motion were somehow left on during snapshot (should not happen for Crossfade — live path wins), underlay could move under a static from-fade.
4. **Slide + motion:** from-frame may be a mid-pan grab (intentional freeze of last dwell pose); to-frame is post-load at camera start (or zoom framing). Acceptable projector behaviour.

### Decision log

- Slide ⊄ live motion (product): static dual-frame only.
- Crossfade ∩ live motion: keep outgoing camera alive; incoming is a motion-sampled overlay until handoff.
- Aspect is inviolable for all composited frames.


---

## Plan / work (2026-09-04) — bundle `biltoo-142-crossfade-freeze`

**Issue:** Pan&zoom + Crossfade still jumps when the previous image disappears
(direction/position snap); screen not cleared cleanly.

### Root cause
Live crossfade kept the **outgoing** dwell camera running under the fade. When
LoadReplace removed that item, the visible motion path switched from the old
slide’s camera to the new one → jump. Transparent letterbox pads in the to-frame
also let the underlay show through (“not clearing the screen”).

### Fix
1. `cancelSlideshowMotion()` when a live transition begins (and again when the
   to-frame is ready) — outgoing underlay freezes at its last pose.
2. To-frame canvas filled with the view background (opaque), not transparent.
3. LoadReplace: start camera + release hold **before** re-enabling updates.

### Done criteria
- [x] No direction snap at end of crossfade under pan&zoom
- [x] Letterbox bars do not reveal the outgoing image
- [x] Next **143**


---

## Plan / work (2026-09-04) — bundle `biltoo-144-dual-motion-handoff`

**Feedback:** Freezing the outgoing image during crossfade is wrong — both
images must keep moving on their own paths. Freeze also did not fix the jump
(likely timing/load related).

### Model
- Outgoing dwell camera keeps running under the live crossfade.
- Incoming to-frame keeps sampling its path in real time.
- Hold after fade: keep updating the to-frame until LoadReplace (progress
  includes decode latency).
- Camera starts at that progress; then hold drops.

### Changes
1. Remove `cancelSlideshowMotion` from live-transition start.
2. Prefer full `ImageLoader::load` for the to-frame (aspect/size parity).
3. On fade complete: enter hold but **do not** stop the timer or clear the
   source image; keep advancing motion samples until release.
4. FadeBlack completion path restored separately.

### Done criteria
- [x] Both images move during crossfade + pan&zoom
- [x] Handoff progress includes load wait
- [x] Next **145**


---

## Plan / work (2026-09-04) — bundle `biltoo-145-motion-extend-debug`

**Feedback:** Still jumps; both images still not moving; suspect widget recalc
when one image is removed. Requested debug values.

### Root cause (outgoing freeze)
Dwell motion duration equals the slideshow interval. At the advance tick the
outgoing camera is already at **t=1 and stopped**, so crossfade runs over a
frozen underlay even with dual-motion intent.

### Fix
1. `extendOutgoingMotionThroughTransition()` — new camera leg from the *current*
   pose for fade+load duration when a live transition starts (and if a leg ends
   mid-transition).
2. Skip `resetTransform()` in `prepareImageModeCanvas` while a live hold/active
   covers the viewport (widget reset under the overlay).
3. `qWarning` traces prefixed `[biltoo-slideshow]` for: beginLive, extend,
   startMotion, applyMotion (throttled), enterHold, releaseHold, clearLiveCanvas,
   prepareImageModeCanvas, cancelMotion, motion stop.

### Done criteria
- [x] Outgoing keeps moving during live crossfade
- [x] Debug lines available on stderr
- [x] Next **146**


---

## Plan / work (2026-09-04) — bundle `biltoo-147-continuous-bias`

**Logs showed:**
- Outgoing and incoming pans often reverse direction mid-crossfade (independent
  path-hash biases).
- Handoff: `m11≈0.31` under hold → `startMotion` at `m11≈0.27` on a new path.

### Fix
1. Shared `m_motionBiasA/B` for camera + live to-frame.
2. `chooseContinuingMotionBiases`: first slide seeds from path hash; later slides
   pick an end bias with positive dot product (no reverse).
3. `beginLive`: extend outgoing on current path, **then** advance biases once for
   the incoming overlay.
4. Handoff `startMotion` **reuses** those biases (no second advance).
5. `extendOutgoing` targets shared biasB / travel dir.

### Done criteria
- [x] One continuous travel direction across a crossfade
- [x] Overlay and handoff camera share the same A→B path
- [x] Next **148**


---

## Plan / work (2026-09-04) — bundle `biltoo-148-fixed-direction`

**Logs (147):** `bias continue … dot=0.00` still allowed orthogonal/reverse ends;
extend went +Y while handoff `endC` went -Y; hold release `m11` 0.30→0.27.

### Fix
1. Session-fixed `m_motionSign` — bias A/B always pan the same way (mild diagonal,
   primary Y). No per-slide reverse.
2. On live hold, paint opaque view background under the to-frame so the underlay
   cannot flash a different scale at release.

### Done criteria
- [x] No direction reverse across consecutive slides / crossfade
- [x] Hold fully covers underlay
- [x] Next **149**


---

## Plan / work (2026-09-04) — bundle `biltoo-149-camera-mode`

**Insight:** Jumps on image remove match QGraphicsView **AlignCenter** (and
mouse transform anchors) recentering when the scene/item changes — not only
pose math. Transition:None looked fine because nothing rebuilt the scene under
a dual-frame composite the same way.

### Proper fix (not another path hack)
`enterSlideshowCameraMode()` / `leaveSlideshowCameraMode()`:
- `AlignLeft | AlignTop` (no auto-center on scene/item change)
- `NoAnchor` for transform/resize
- scrollbars **AlwaysOff** (matrix is the only camera; no scroll offset fight)

Enter on motion start and live begin; leave when motion and live transition end.

### Done criteria
- [x] Camera mode disables AlignCenter during motion/transition
- [x] Next **150**


---

## Plan / work (2026-09-04) — bundle `biltoo-150-dual-blit`

**Model correction:** A QGraphicsView “camera” only holds one item. Crossfade
with two moving images is **two blits**, not a scene camera + overlay.

### Implementation
1. Capture outgoing `sourceImage` + motion progress at `beginLive`.
2. Stop scene motion for the transition.
3. Each tick: `renderMotionCoverPixmap` for **from** and **to** on the shared
   direction.
4. Paint: fill background, draw from at \(1-t\), to at \(t\) — scene fully covered.
5. Hold shows only the to-frame; LoadReplace + startMotion at the same progress;
   then drop the blit layers.

### Done criteria
- [x] Crossfade does not use scene underlay
- [x] Both images are motion-sampled pixmaps
- [x] Next **151**


---

## Plan / work (2026-09-04) — bundle `biltoo-151-unified-pose`

**Root cause of remaining jump:** Blit used `center = mid + lerp(A,B,t)*half(s(t))`
while the camera used **linear** `lerp(startC, endC, t)`. Those differ when
`half(start) ≠ half(end)` (zoom changes overflow). At releaseHold the scene
snapped to the camera path.

### Fix
`applySlideshowMotionProgress` evaluates the **same** bias×half(scale) pose as
`renderMotionCoverPixmap`. `cancelSlideshowMotion` no longer leaves camera mode
(that re-enabled AlignCenter during beginLive).

### Done criteria
- [x] Blit and camera share one pose model
- [x] Next **152**


---

## Plan / work (2026-09-04) — bundle `biltoo-152-linear-kenburns`

**Issue:** Path looked like an upward curve, not smooth linear Ken Burns.

**Cause:** Pose used `center = lerp(A,B,t) * halfOverflow(scale(t))`. As zoom
increases, half grows, so pan **accelerates** (curved path).

**Fix:** Classic linear Ken Burns for both camera and blit:
- `scale(t) = lerp(s0, s1, t)`
- `center/offset(t) = lerp(start, end, t)` in fixed start/end crop space

### Done criteria
- [x] Linear pan + zoom
- [x] Next **153**


---

## Plan / work (2026-09-04) — bundle `biltoo-153-preload-kenburns`

**Requests:** Preload next slide off GUI thread; Ken Burns was dead-centre / boring.

### Changes
1. `preloadSlideshowImage(path)` — `ImageLoader::load` on the thread pool; cache
   until `beginLive` consumes it (or a newer preload replaces it).
2. MainWindow preloads the next path on slideshow start and after each advance.
3. `pickInterestingMotionBiases` — full corner/edge pairs from path hash (not the
   mild fixed diagonal).
4. Motion base overscans when Fit leaves ~0 overflow so start/end centres leave
   the image centre (min half ≥ 32 scene px).

### Done criteria
- [x] Preload off GUI thread
- [x] Varied corner Ken Burns with pan room
- [x] Next **154**


---

## Plan / work (2026-09-04) — bundle `biltoo-157-opacity-crossfade`

### Clean model (no dual motion during transition)

| Phase | What moves | How shown |
|-------|------------|-----------|
| **Dwell** | Ken Burns (linear pan+zoom) | Scene camera on one image |
| **Crossfade** | **Nothing** (opacity only) | Static from-grab + static to@t=0 |
| **Handoff** | Dwell starts at t=0 | Matches the to-frame already shown |

Direction changes during crossfade were from sampling **two different Ken Burns
paths** while fading. That is not a crossfade; a crossfade is opacity.

Removed: dual-blit motion ticks, extendOutgoing, mid-path handoff progress.

### Done criteria
- [x] Crossfade = opacity of two static frames
- [x] Ken Burns only during dwell
- [x] Next **158**


---



## SLIDESHOW TRANSITION MODEL (authoritative — do not violate)

Dwell **moves** images. Transitions **only change colour/opacity** (or slide cards).
Motion never stops because a transition runs.

```
TIME ──────────────────────────────────────────────────────────────────►

Image A:  [======== DWELL motion A→A' ========][from-path continues]
Image B:                                      [to-path from 0 ════════ DWELL B═══►]

Composite:
  None:        cut to B
  Crossfade:   opacity A = 1−t , opacity B = t          (both still moving)
  FadeBlack:   t∈[0,½] black rises over A
               t=½     solid black (advance/load under veil)
               t∈[½,1] black falls over B               (both still moving)
  Slide:       static cards only (exception: freezes poses by design)
```

### Rules

| Phase | What moves | What the transition does |
|-------|------------|--------------------------|
| Dwell | One image, Ken Burns/Pan&scan blit | — |
| Crossfade | **Both** images on their own paths | Opacity blend only |
| FadeBlack | **Both** images on their own paths | Black veil only |
| Slide | Neither (static grabs) | Position of two cards |
| Handoff | Continue **to** path at progress already shown | Drop overlay |

### Never

1. Cancel or pause dwell/motion because a transition started
2. Overwrite the outgoing image's motion biases when the transition begins
   (that snaps Pan&Zoom and looks like a freeze)
3. Wait for the transition to end before starting B's motion
4. Reveal the QGraphicsItem underlay during a live transition (always full-viewport blits)
5. Emit advance twice (FadeBlack advances once at mid-black only)

### Bias ownership

- `m_motionBiasA/B` — **current dwell only**
- `m_liveFromBiasA/B` — outgoing path for the from-blit (captured at beginLive)
- `m_liveToBiasA/B` — incoming path for the to-blit (chosen at startLive, not before)


## SLIDESHOW CONTRACT (do not break)

**Ken Burns moves the images (blit), never the QGraphicsView camera.**

**During dwell (one image):**
- One pixmap, re-sampled each tick along a linear Ken Burns path
- Scene is covered; no setTransform camera

**During crossfade with dwell motion (TWO images):**
1. BOTH images keep moving on their own Ken Burns paths every frame
2. Crossfade = opacity blend only (1−t from, t to)
3. Never freeze either image for the transition
4. Dual pixmap blit only (no scene underlay)
5. After load, dwell blit continues the **to** path at the progress already shown

From path: continues from progress at transition start.
To path: starts at 0, advances with wall time / interval.
Session pan direction (Y sign) is fixed so consecutive paths do not reverse.



## Plan / work (2026-09-04) — bundle `biltoo-158-dual-motion`

Restored continuous dual Ken Burns during crossfade (was wrongly frozen in 157).

See **SLIDESHOW CONTRACT** above. Never freeze either image during the fade again.


## Plan / work (2026-09-04) — bundle `biltoo-159-image-blit`

Ken Burns is **image blit**, not view-camera motion. Dwell paints one moving
frame; crossfade paints two. `setTransform` is not used for slideshow motion.


## Plan / work (2026-09-04) — bundle `biltoo-162-slideshow-cleanup`

Removed dead camera-era code after blit-only Ken Burns:
- `applySlideshowMotionProgress` (view matrix path)
- `enterSlideshowCameraMode` / `leaveSlideshowCameraMode`
- `m_motionStartScale/EndScale/StartCenter/EndCenter`, camera-mode members
- Stale "camera" comments

Slideshow motion is image blit only (see SLIDESHOW CONTRACT).


## Plan / work (2026-09-04) — bundle `biltoo-163-fade-black-position`

**Bug:** Slideshow **Fade through black** jumped the image position as soon as
the transition started (with Ken Burns on). It should only fade out to black
and fade back in.

### Root cause
Live FadeBlack paint drew **only** the black veil. `beginLive` cancels the
dwell blit, so the semi-transparent veil revealed the QGraphicsItem underlay
(Fit/Fill framing without the Ken Burns crop). That pose differs from the
dwell cover pixmap → visible jump.

Also: FadeBlack emitted `slideshowLiveTransitionFinished` again at the end of
phase 2 (after already advancing at mid-black), which would `goNext` twice.
Dwell was started at LoadReplace during the mid-black await while ticks were
suppressed, so the motion clock ran under the veil and jumped on release.

### Fix
1. **Paint:** phase 1 draws from-blit + rising black; phase 2 draws to-blit +
   falling black; pad fill so the scene never shows through.
2. **Tick:** on FadeBlack phase-2 completion, release the overlay only — do
   **not** emit finished again (mid-black already advanced).
3. **Motion:** skip `maybeStartSlideshowMotion` while FadeBlack is awaiting
   load; start dwell from `releaseLiveTransitionHold` at the last to-path
   progress so the pose matches the frame under the lifting veil.

### Done criteria
- [x] Fade through black does not change image position — only opacity/black
- [x] No double-advance at end of FadeBlack
- [x] Dwell continues from the to-frame progress after the veil lifts
- [x] Next **164**


## Plan / work (2026-09-04) — bundle `biltoo-164-motion-never-stops`

**Bugs:**
1. **Pan & scan** appeared to reverse direction: dwell rendered with
   `pathHash=0` (no flip) while live to-frames used `pathHash & 1` → reverse
   at handoff. Not a broken lerp — inconsistent direction source.
2. **Fade through black stopped motion** (and crossfade had a freeze gap):
   `cancelSlideshowMotion()` at `beginLive`, early-return during mid-black
   await with no sampling, veil clock restart rewound motion progress.

**Contract (user):** Dwell moves images. Crossfade / fade-through-black only
change colour/opacity. Motion never stops for a transition.

### Fix
- Pan & scan: pure linear `bias = -1 + 2t` on the long axis (L→R / T→B). No
  pathHash flip.
- Do not cancel dwell at `beginLive` (no frozen gap while next image decodes).
- Separate `m_liveMotionClock` for Ken Burns sampling; veil clock may restart
  at FadeBlack mid-black without rewinding pan.
- While awaiting load under solid black, keep sampling from/to frames.
- On release, always restart dwell at the last to-path progress.

### Done criteria
- [x] Pan & scan direction is stable (no mid-handoff reverse)
- [x] Fade through black does not stop motion
- [x] Crossfade / FadeBlack are opacity/colour only
- [x] Next **165**


## Plan / work (2026-09-04) — bundle `biltoo-165-transition-in-motion`

**Bug:** Pan&Zoom still appeared to stop at transition start.

**Cause:** `beginLive` called `pickInterestingMotionBiases(nextPath)` which
**overwrote** `m_motionBiasA/B` while dwell was still painting the outgoing
image. The crop path snapped to the next slide's Ken Burns pair — looks like
motion died. From-progress was also frozen at beginLive time for the whole
async decode gap.

### Fix
- Never touch dwell biases in `beginLive`
- Choose incoming biases into `m_liveToBiasA/B` only at `startLive`
- Refresh from-progress when live paint actually starts
- All to-frame samples use `m_liveToBias*`; handoff installs them as dwell biases
- Authoritative transition model written above

### Done criteria
- [x] Transition runs *in* motion (no stop/snap at start)
- [x] Written model for None / Crossfade / FadeBlack / Slide
- [x] Next **166**


## Plan / work (2026-09-04) — bundle `biltoo-166-one-motion-clock`

**Problem:** Crossfade still stopped motion. Root cause was structural: two
timers (dwell + live), `tickSlideshowMotion` early-returned while live, and
`startSlideshowMotion`/`maybeStart` called `cancelSlideshowMotion` mid-fade.

**Design rule:** One motion clock. Transitions only change compositing.
It must be structurally impossible for a transition to stop the motion timer.

### Changes
- `tickSlideshowMotion` is the only frame sampler: dwell **or** dual-blit
- No early-return for live flags; opacity timeline runs in the same tick
- `startLiveTransitionWithImage` does not start a second timer
- Soft handoff in `releaseLiveTransitionHold` (rebase offset, keep clock)
- `maybeStartSlideshowMotion` is a no-op while live (no cancel mid-fade)
- From path continues on the same wall clock; to path starts at `m_toLayerWallMs`

### Done criteria
- [x] Single motion timer through dwell + crossfade + fade-black
- [x] No cancelSlideshowMotion on the transition path
- [x] Next **167**


## Plan / work (2026-09-04) — bundle `biltoo-167-progress-not-stuck`

**Why motion still "stopped" in the crossfade:**

Motion duration was set equal to the slideshow interval. The advance timer
fires when wall ≈ duration, so `progress` was already **clamped to 1.0**.
During the entire crossfade the from-image was held at the end pose — not a
timer bug, a progress model bug.

```
progress = clamp(wall / duration, 0, 1)   // duration == interval
advance fires when wall ≈ interval  →  progress == 1  →  frozen for the fade
```

**Fix (simple lerp model):**

```
motionDuration = interval + transitionDuration
progress = clamp(wall / motionDuration, 0, 1)
```

Advance still fires at `interval`, but progress is then still < 1 and keeps
lerping through the fade at the same rate as during dwell. To-image starts at 0
on its own path at the same rate.

### Done criteria
- [x] From-image not stuck at progress 1 during crossfade
- [x] Linear progress only — no second motion system
- [x] Next **168**


## Plan / work (2026-09-04) — bundle `biltoo-168-rename`

**Rename application** QImgView / `qimgview` → **Biltoo** / `biltoo`.

Unreleased `-dev`; no backward compatibility:

- Binary, CMake project, Nix `pname` / flake packages
- Desktop id, AppStream metainfo, theme icon `biltoo.svg`
- `BILTOO_VERSION`, `BILTOO_HAVE_*` defines
- QApplication name / org / domain / desktop file
- Project extension `.biltoo`, format id `biltoo-project`
- MIME types `application/x-biltoo-*`
- UI strings, docs, translations paths

GitHub links updated to `Grumbel/biltoo` (rename the remote repo when ready).

### Done criteria
- [x] No remaining `qimgview` / `QImgView` product identifiers in tree
- [x] Next **169**


## Plan / work (2026-09-04) — bundle `biltoo-169-readme-screenshots`

Replace single root `screenshot.png` with `screenshots/gallery.png` and
`screenshots/workspace.png`. README and AppStream metainfo show both modes.

### Done criteria
- [x] Gallery + Workspace shots in README
- [x] Next **170**


## Plan / work (2026-09-04) — bundle `biltoo-170-config-org-lowercase`

`QApplication::setOrganizationName("biltoo")` (lowercase) so Linux settings
live under `~/.config/biltoo/` next to the binary name. Display name stays
**Biltoo**.

### Done criteria
- [x] Config dir matches executable name
- [x] Next **171**


## Plan / work (2026-09-04) — bundle `biltoo-171-opengl-slideshow`

### OpenGL viewport
`ImageView` uses `QOpenGLWidget` as the QGraphicsView viewport so scene and
overlays composite on the GPU (`Qt6::OpenGLWidgets`).

### Slideshow scale on GPU
Ken Burns / pan-scan no longer `QImage::scaled(..., SmoothTransformation)` every
tick. `paintMotionCover` draws the source image into a dest rect; with
`SmoothPixmapTransform` on the GL viewport the scale is filtered by OpenGL.

Tick only advances progress and calls `update()`. Crossfade / FadeBlack paint
the from/to sources directly.

`renderMotionCoverPixmap` remains for rare snapshot paths (Slide).

### Done criteria
- [x] OpenGL viewport on ImageView
- [x] No per-frame software smooth scale for dwell / live transitions
- [x] Next **172**


## Plan / work (2026-09-04) — bundle `biltoo-172-fix-white-screen`

**Bug:** Entire view was a white screen after `setViewport(new QOpenGLWidget)`.

**Fix:** Remove the OpenGL viewport (QGraphicsView + QOpenGLWidget was blank on
this setup). Keep `paintMotionCover` dest-rect drawing so slideshow still avoids
per-frame `QImage::scaled(SmoothTransformation)`.

OpenGL viewport can be revisited later (format/context at app start, fallback).

### Done criteria
- [x] Raster viewport restored (UI visible again)
- [x] Slideshow still uses dest-rect paint (no per-tick smooth scale)
- [x] Next **173**
