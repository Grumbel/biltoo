# QImgView TODO

## Core Vision

QImgView is a classic Qt image viewer with three presentation modes on one canvas:

- **Image** — single-image browsing (zoom, pan, edge nav, slideshow)
- **Gallery** — session overview with packaged layouts (not free-form editing)
- **Workspace** — free-form multi-image canvas (move, handles, opacity, z-order)

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
- Broader undo coverage (Workspace membership hide/show, sort reorder, layout switch)
- Optional confirmation dialogs for destructive session replace (drop/Open)

### Stability / polish (optional)

- [ ] Thumbnail click crash (needs reliable repro)
- [ ] CLI `--workspace` / start-in-workspace still slightly confusing vs Preferences
- [ ] Drag-load status: show count of images still decoding
- [ ] AppStream screenshots for Flathub-style stores
- [ ] Virtualize Gallery when sessions are huge (thousands of tiles)
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
- [x] flake `apps.default` `meta.description`
- [x] `.desktop` uses `%F` for multiple images

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
