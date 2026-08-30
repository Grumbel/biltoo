# QImgView TODO

## Core Vision

QImgView is a classic Qt image viewer that treats the image area as a **workspace** rather than a restricted single-image viewport.

## Done

- [x] Classic Qt6 app: menus, toolbar, theme icons, context menu, fullscreen chrome
- [x] Multi-image session, async thumbnails, slideshow, sort, append, rich CLI
- [x] App icon (SVG), .desktop file, toolbar layout with fullscreen on the right
- [x] Workspace foundation (ImageItem, multi-item view, Ctrl+drop, side-by-side)
- [x] Free continuous rotation (Shift + drag around item centre)
- [x] Opacity up/down/reset for overlap comparison
- [x] Raise / Lower z-order
- [x] Double-click thumbnail → add image onto workspace
- [x] Clear workspace extras (keep primary)

- [x] Esc exits fullscreen; toolbar toggle is Ctrl+T (not Tab)
- [x] Natural/locale-aware filename sort (QCollator numeric mode)
- [x] Preferences dialog (slideshow interval, default sort)
- [x] Metadata side panel (Qt text keys + file info; Ctrl+E)
- [x] Workspace mode optional (off by default); DnD adds when enabled
- [x] Workspace canvas persists while mode is off; classic view centres session image
- [x] Undo/redo for workspace transforms; left Select/Pan tool strip
- [x] Workspace thumbnail bar: click toggles image on/off the canvas
- [x] Per-image position/scale/rotation remembered while deselected
- [x] On-canvas scale (corner) and rotate handles on selected items
- [x] Draggable splitter between image view and thumbnail bar (resizable thumbs)
- [x] Drop appends to session + workspace in workspace mode
- [x] Slideshow auto-fullscreen preference; slideshow disabled in workspace mode
- [x] Thumbnail bar position: bottom or left (View menu)
- [x] Metadata dock close button stays in sync with toolbar/menu toggle
- [x] Image mode: left/right edge click = prev/next; hover arrows; double-click fullscreen
- [x] libvips optional fallback loader (Qt first, then vips); dynamic file filters
- [x] Packaged layouts scale images to fit the viewport; free-form positions restored
- [x] Masonry (column-pack) layout
- [x] Workspace mode: zoom buttons / wheel zoom the view (not individual items)
- [x] QA: filter non-images in expandPaths; restore metadata after fullscreen
- [x] QA: keep workspace thumbnail selection after sort; metadata fallback dimensions
- [x] Image-mode zoom is view-level (consistent with Fit / 1:1)
- [x] Persist free-form view transform (pan/zoom) when switching packaged layouts and back
- [x] Live rotation/scale readout while dragging handles (status bar)
- [x] Raise / lower and opacity controls on the selection chrome

## Near-term

- [ ] Snap / align helpers
- [x] Richer Exif via libexiv2 (optional; Summary + Exif/IPTC/XMP groups)
- [ ] Depend on qtimageformats / KImageFormats for more formats via Qt plugins
- [x] Optional thumbnail bar on the right (and top)
- [ ] Masonry taller than viewport: use View → Show Scrollbars, or pan (middle / Alt)
- [x] Masonry column width on toolbar when Masonry is active (not Preferences)
- [x] Gallery layouts vs free-form Workspace: packaged layouts non-movable,
      click → Image mode, Back to Gallery / Esc, Workspace Mode forces Free Form,
      entering a gallery layout places the full session on the canvas
- [x] Gallery is a first-class ImageView mode (not a Workspace layout): View → Gallery
      menu, toolbar icons, no free-form handles outside Workspace
- [ ] Gallery polish: exclusive mode indicator in status/toolbar; drop Free Form layout
      action entirely; single shared “session multi” flag instead of overloading
      m_workspaceMode for Gallery

### UX backlog (evaluated)

Priority roughly: bugs first, then small polish, then larger features.

- [x] **Preferences dialog crashes** — was null `m_masonryWidthSpin` (used in form without construction).

- [x] **Drag&drop on image area (not only thumb bar)** — ImageView accepts drops and emits `filesDropped`; MainWindow shares handler with window drops.

- [x] **Slideshow: Space starts; leaving fullscreen stops** — Space toggles; leaving fullscreen stops.

- [x] **Scrolling workspace misaligns Image mode** — reset transform and scrollbars when leaving workspace and when loading Image-mode files.

- [x] **Zoom icons to the right of the toolbar (with separator)**

- [x] **Undo/redo on main toolbar (not only left strip)**

- [x] **Colour swatch in status bar**

- [x] **Zoom to fill** (cover window, crop edges)

- [x] **Image mode pan preference** — left-drag pan toggle in Preferences.

- [x] **HUD overlay** — View → Show HUD Overlay / H (VCR auto-fade still open).

- [x] **Thumb “crop to square” mode**

- [x] **Hide thumbnail filenames**

- [x] **HFlip / VFlip**

- [x] **Rotate handles on all four sides**

- [x] **Larger handles in general**



### Thumbnail bar interaction (settled)

- **Image mode:** click = navigate session; Ctrl/Shift+click = add that file to workspace.
- **Workspace mode:** click = toggle on canvas; Shift+click = range from anchor; Ctrl+click = toggle one.
  No rubber-band rectangle select. Alt+drag still starts a file drag.
- **Ctrl+A:** Select All thumbnails (enters workspace if needed, shows all on canvas).
- **Add Images:** Ctrl+Shift+A (not Ctrl+A).
- **[+]** on each thumb rejected as redundant with Add / Ctrl+click / workspace toggle.

## GUI / HIG review (GNOME 2 + common desktop conventions)

Review date: 2026-08-30. Aim: classic single-document image viewer feel.

### Applied (this pass)

- [x] Preferences under **Edit** (GNOME 2), not File
- [x] Add Images shortcut **Ctrl+Shift+A** (Ctrl+A reserved for Select All by convention)
- [x] Preferences grouped with **QGroupBox** (Slideshow / Session / View)
- [x] Slideshow interval shown in **seconds** (stored as ms)
- [x] Preferences buttons via **QDialogButtonBox** (OK default, Cancel)
- [x] Window title **filename — QImgView** when a file is open
- [x] **Home / End** → first / last image (Go menu + context menu)

### Still open — structural / larger effort

- [x] **View menu information architecture** — submenus: Zoom, Image, Thumbnails,
  Sort, Workspace.

- [x] **Keyboard map** (see below).

### Keyboard map (reference)

| Key | Action |
|-----|--------|
| F / F11 | Fullscreen |
| Esc | Leave fullscreen (stops slideshow) |
| Space | Start/stop slideshow |
| ←/→, PgUp/PgDn | Previous / next |
| Home / End | First / last |
| F (Fit removed) | Fullscreen only; Fit via toolbar/menu |
| Ctrl+0 | 1:1 zoom |
| Ctrl++/− | Zoom in/out |
| Ctrl+F | Zoom to fill |
| [ ] / Ctrl+L / Ctrl+R | Rotate |
| Ctrl+H / Ctrl+Shift+H | Flip H / V |
| H | Toggle HUD |
| Ctrl+A | Select all thumbnails |
| Ctrl+Shift+A | Add images |
| Ctrl+O | Open |
| Ctrl+T | Toggle toolbar |
| Ctrl+E | Metadata |
| Delete | Remove from workspace / session (context) |
| Shift+drag (workspace) | Free rotate; Ctrl constrains 90° |
| Handle rotate + Shift/Ctrl | Snap 45° / 90° |

- [x] **Select All** — Ctrl+A selects all thumbnails; workspace shows them on canvas.

- [ ] **Toolbar customisation / density** — *Later.*
  GNOME 2 allowed hideable toolbars (we have Ctrl+T). Optional icon+text style
  preference is low priority while icons + tooltips remain clear.

- [x] **Accessible names** for image view and thumbnail bar (edge nav described).
  Left/right nav zones are mouse-only; ensure screen-reader / keyboard path
  stays first-class via Go menu (already present).

- [x] **Consistent disabled feedback** — nav/slideshow image-mode only; rotate/flip
  need an item; zoom gated on content; Select All needs files.

- [x] **Failed load feedback** — status line shows “Failed to load: …” / “Loading …”;
  previous image kept until decode succeeds (no empty flash on rapid nav).


## Broader image formats (research notes)

Prefer staying on the `QImageReader` path where possible; libvips is the fallback.

1. **qtimageformats**: WebP, TIFF, TGA, ICNS, JP2, … via official Qt plugins.
2. **KImageFormats** (runtime): AVIF, HEIF, JXL, OpenEXR, PSD, QOI, camera RAW (LibRaw), …
3. **libvips** (done, optional at build time): decode when Qt fails; thumbnail shrink-on-load.
4. **OpenImageIO**: only if EXR/VFX becomes a first-class goal (heavier deps).
5. Avoid FreeImage (maintenance); avoid ImageMagick as the primary loader.

## Later

- [ ] OpenGL path for large images / many items
- [ ] Persist workspace item state across sessions
- [ ] Per-image view state persistence in Image mode
- [x] Thumbnail bar top / right positions
- [ ] Animated GIF / multi-page TIFF frame navigation
- [ ] Colour-managed display (ICC / OCIO) if format backends expose profiles

## Interaction summary

| Gesture | Effect |
|---------|--------|
| Drop (Image mode) | Replace session |
| Shift/Ctrl+Drop (Image mode) | Append to session (thumbnail bar) |
| Drop (Workspace mode) | Append to session and show on workspace |
| Click thumbnail (Workspace) | Toggle image on/off the workspace (state kept) |
| Ctrl/Shift+click thumbnail (Image mode) | Enable workspace mode and add image |
| Click left/right edge (Image mode) | Previous / next image (hover shows arrows) |
| Double-click image (Image mode) | Toggle fullscreen |
| Click item (Workspace) | Select (shows scale/rotate handles) |
| Drag item | Move |
| Drag corner handle | Uniform scale about centre |
| Drag rotate handle | Free rotate about centre |
| Shift+Drag on item | Free rotate (legacy) |
| Wheel (Image mode) | Zoom current image |
| Wheel (Workspace) | Zoom the view about the cursor |
| Zoom buttons (Workspace) | Zoom the view about centre |
| Alt+LMB / Middle | Pan view |
| Delete | Remove selected from workspace (state kept) |
| Ctrl+Y | Side-by-side layout |
| Ctrl+Shift+W | Clear workspace extras |
| Ctrl+Shift+Up/Down | Raise / Lower |
| Ctrl+= / Ctrl+- | Opacity up / down |
| Esc | Exit fullscreen |
| Ctrl+T | Toggle toolbar |
| View → Thumbnails on Bottom/Left | Thumbnail bar placement |
| View → Layout Masonry | Column packing (Pinterest-style) |
| View → Show Scrollbars | Toggle scrollbars |
| F11 | Toggle fullscreen |
| Ctrl+E | Toggle metadata panel |
| F5 | Slideshow (Image mode only; optional fullscreen) |
