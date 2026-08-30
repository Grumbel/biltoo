# QImgView TODO

## Core Vision

QImgView is a classic Qt image viewer with three presentation modes on one canvas:

- **Image** — single-image browsing (zoom, pan, edge nav, slideshow)
- **Gallery** — session overview with packaged layouts (not free-form editing)
- **Workspace** — free-form multi-image canvas (move, handles, opacity, z-order)

## Done (recent)

- [x] Gallery as its own ViewMode (not a Workspace layout)
- [x] Packaged layouts: Horizontal, Vertical, Grid, Grid Crop, Masonry, Masonry Rows, Stack
- [x] Gallery → Image on item open; **Up** returns with scroll/selection restore
- [x] No move/handles outside Workspace; checkerboard optional via Preferences
- [x] Configurable background colour / pattern
- [x] VERSION file + Nix/CMake version wiring; separateDebugInfo
- [x] Code layout: MainWindow / ImageView / ImageItem split TUs; GalleryLayout; icons helpers

## Open

### Gallery / UX

- [ ] Gallery rubber-band multi-select (optional)
- [ ] Grid / Grid-Crop column count control (like masonry spin)
- [ ] Keyboard focus ring / arrow-key navigation between gallery tiles
- [ ] Remember last Gallery layout across sessions (QSettings)
- [ ] Interaction summary / shortcuts still list a few pre-Gallery names (keep in sync)

### Stability / polish

- [ ] Thumbnail bar click crash: need better reproduction if still seen
- [ ] Workspace item misalignment after mode switches: re-check after long sessions
- [ ] Undo stack coverage for gallery open/return (currently workspace-oriented)

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
| Click gallery tile | Open Image mode for that file |
| **Up** / Esc (from Image after gallery open) | Return to Gallery |
| Click left/right edge (Image) | Previous / next |
| Double-click (Image) | Toggle fullscreen |
| Click item (Workspace) | Select (handles) |
| Drag item (Workspace only) | Move |
| Drag handles (Workspace only) | Scale / rotate |
| Wheel | Zoom view (mode-dependent) |
| Alt+LMB / Middle | Pan |
| Delete (Workspace) | Remove from canvas (state kept) |
| View → Gallery layouts | Enter Gallery with packing |
| View → Workspace Mode | Free-form canvas |
| F5 | Slideshow (Image only) |
| F11 | Fullscreen |
| Ctrl+E | Metadata panel |
| Ctrl+T | Toolbar |

## Broader image formats

Prefer `QImageReader`; libvips optional fallback. See AGENTS.md / prior notes for plugin research.

