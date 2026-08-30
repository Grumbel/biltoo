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

## Open

### Reported bugs (2026-08-30)

- [x] F/F11 only enters fullscreen; cannot leave
- [x] Drag&drop replaces the image set instead of adding (Gallery appends; Image still replace / Shift-append)
- [x] Drag from thumbnail bar onto Gallery duplicates; Gallery ignores session re-drops (Workspace OK)
- [x] Space does not pause slideshow in fullscreen
- [x] Workspace edge resize handles should be thicker
- [x] Mouse interaction with rotated handles / highlight disconnect (view-owned hover + larger hits)
- [x] Workspace handles drawn at image stacking order; paint on top of all images
- [x] Clicking near workspace handles can deselect the image
- [x] Handles: bigger; input/highlight handles scale+rotation
- [x] Checkerboard LOD snap when cells would go below ~16×16 screen px

### Gallery / UX

- [x] Horizontal / Vertical near-fullscreen highlight: classic multi-select
      (click / Ctrl / Shift / rubber-band) + double-click to open; no hover wash

- [ ] Gallery rubber-band multi-select (optional)
- [x] Grid / Grid-Crop column count control (like masonry spin)
- [x] Keyboard focus / arrow-key navigation between gallery tiles (spatial)
- [x] Remember last Gallery layout across sessions (QSettings `lastGalleryLayout`)
- [x] Interaction summary / shortcuts synced with current modes and keys
- [x] Rename ThumbnailBar “workspace mode” → multi-select (`setMultiSelectEnabled`)

### Stability / polish

- [ ] Thumbnail bar click crash: need better reproduction if still seen
- [ ] Workspace item misalignment after mode switches: re-check after long sessions
- [ ] Undo stack coverage for gallery open/return (currently workspace-oriented)

### Preferences / desktop integration

- [x] Default application tab: checkbox reflects current default; toggle applies immediately; mark-all / remove-all buttons
- [x] Preferences dialog button order: GNOME 2 HIG (Cancel left, OK right); document in AGENTS.md
- [x] Add `nix flake check` phase

### Later

- [ ] OpenGL path for large images / many items
- [ ] Persist workspace item state across sessions
- [ ] Per-image view state persistence in Image mode
- [ ] Animated GIF / multi-page TIFF frame navigation
- [ ] Colour-managed display (ICC / OCIO) if format backends expose profiles

## Interaction summary

| Gesture | Effect |
|---------|--------|
| Drop (Image mode) | Replace session |
| Shift/Ctrl+Drop (Image mode) | Append to session |
| Drop (Workspace) | Append session + canvas |
| Drop (Gallery) | Append session + relayout |
| Click gallery tile | Select (Ctrl toggle, Shift range, rubber-band) |
| Double-click gallery tile / Enter | Open Image mode for that file |
| Gallery arrows (spatial) / Home / End | Move session cursor among tiles |
| **Up** / Esc / top edge (from Image after gallery open) | Return to Gallery |
| Click left/right edge; ←/→ (Image) | Previous / next |
| Hover gallery tile | HUD shows filename |
| Double-click (Image) | Toggle fullscreen |
| Click item (Workspace) | Select (handles) |
| Drag item (Workspace only) | Move |
| Drag handles (Workspace only) | Scale / rotate / opacity / chrome |
| Ctrl+D (Workspace) | Duplicate selection |
| Wheel | Zoom view (Image/Workspace); Gallery scrolls, Ctrl+wheel zooms (plain wheel zooms if no overflow) |
| Alt+LMB / Middle | Pan |
| Delete (Workspace) | Remove from canvas (state kept) |
| View → Gallery layouts | Enter Gallery with packing |
| View → Workspace Mode | Free-form canvas |
| Space | Slideshow start/stop (Image; from Gallery enters Image) |
| F / F11 | Fullscreen |
| H | HUD overlay (pin) |
| Ctrl+E | Metadata panel |
| Ctrl+T | Toolbar |
| Ctrl+0 / Fit actions | Zoom 1:1 / fit / fill |

## Broader image formats

Prefer `QImageReader`; libvips optional fallback. See AGENTS.md / prior notes for plugin research.

