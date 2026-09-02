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
  - [ ] Gallery → Workspace must restore durable Workspace snapshot (or empty), never import Gallery packing
  - [ ] Explicit Workspace **Layout** action (selected tiles → packaged arrangement) with adjustable parameters; do not overload Gallery mode switches for this
- [ ] Session-image identity acceptance (see SESSION.md §4–§5):
  - [x] Duplicate / drop-duplicate: flip/crop one tile does not affect the other
  - [ ] Image-mode crop of slot B updates only Workspace tile B after return
  - [x] Gallery open of a duplicated path uses session id (not first path match)
  - [x] Session remove undo restores stable ids / Workspace association
  - [x] No unbound Workspace tiles after normal add/dup/drop paths
- [ ] Fix remaining crashes, layout edge cases, and selection/context-menu bugs
- [ ] Keyboard shortcuts, HUD, slideshow, and desktop integration as documented
- [ ] Packaging (Nix/CMake), AppStream, `.desktop`, i18n scaffolding
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
