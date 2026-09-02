# QImgView

A classic desktop image viewer with three ways to look at a session on one canvas:

- **Image** — browse one file at a time (zoom, pan, rotate, flip, slideshow)
- **Gallery** — see the whole session laid out (strip, grid, masonry, …)
- **Workspace** — arrange several images freely for comparison

![QImgView main window](screenshot.png)

## Features

**Session**

- Open files or directories; append more via File → Add, drag-and-drop, or the command line
- File → Open replaces the session; File → New clears it
- History menu to reopen recent sessions
- Sort by name, date, file size, width, height, or pixel count
- Thumbnail bar (show/hide, edge placement, optional labels)

**Image mode**

- Zoom in/out, 1:1, fit, fill, and rubber-band zoom to a region
- Pan, rotate (±90°), flip horizontal/vertical
- Slideshow with adjustable speed (`[` / `]`); optional fullscreen while playing
- Edge navigation and keyboard prev/next/first/last

**Gallery**

- Layouts: side-by-side, vertical strip, grid, masonry (columns or rows), masonry fill (rectangular)
- Grid-crop layout is temporarily disabled (session crop conflict)
- Multi-select; double-click (or Enter) opens Image mode
- Return to Gallery with viewport restored

**Workspace**

- Free placement: move, scale, rotate, opacity, raise/lower
- Durable canvas: switching to Gallery and back restores the Workspace snapshot
- Layout panel: pack the current selection (same algorithms as Gallery) without leaving Workspace
- Rubber-band multi-select; transform handles on the selection
- Open the current selection in a new window
- Delete removes from the canvas only (session membership stays)

**Crop** (Image or single Workspace selection)

- Draft rectangle with edge/corner grips, **centre move grip**, rotate knob, Expand
- Close commits (axis-aligned bake; optional pad); Cancel/Esc discards
- Ctrl resize from centre; Shift square or 15° rotate snap
- Session/project store crop rect and rotation (non-destructive until export)

**General**

- **Page Setup**, Print, Print Preview, and **Export PDF** (File); Workspace **Print Page Guide** (View) matches the app page size
- Fullscreen, on-canvas HUD, configurable background (solid or checkerboard)
- Metadata side panel (file info; colour/structure extras when available)
- Preferences for slideshow, start mode, thumbnails, and default applications
- Theme icons and standard desktop shortcuts

## Usage

```bash
qimgview [options] [files-or-directories…]
```

| Option | Description |
|--------|-------------|
| `-r`, `--recursive` | Expand directories recursively |
| `--start-at=N` | Start at the *N*-th image (1-based) |
| `--sort=name\|mtime` | Sort by name or modification time |
| `--mode=image\|gallery\|workspace` | Start in Image, Gallery (masonry), or Workspace |
| `--slideshow` | Start slideshow after loading |
| `--interval=ms` | Slideshow interval in milliseconds |
| `-f`, `--fullscreen` | Start in fullscreen |
| `--thumbnails` / `--no-thumbnails` | Force the thumbnail bar on or off |

Drag-and-drop onto the window **appends** to the session. Use **File → Open** to replace it.

## Image formats

Common formats go through Qt. Extra types such as GIMP `.xcf` (and related plugin formats) work when **KImageFormats** is installed. The Nix package includes that dependency.

## Install / build

**Nix**

```bash
nix run github:Grumbel/qimgview
# or, from a checkout:
nix develop    # shell
nix build      # package
nix run        # run
```

**CMake** (Qt 6 Widgets)

```bash
mkdir build && cd build
cmake ..
cmake --build .
./qimgview
```

Optional libraries (vips, exiv2, …) are detected at configure time when present.

## Project files and export

- **Open/Save Project** (`.qimgview`): JSON session document with per-image
  `SessionImageId`, non-destructive appearance, optional Workspace poses, and
  **SHA-256** content addressing for external files (relink on load if paths moved).
- **Export PNG**: rasterize Workspace **content bounds** or the **page guide** at a
  chosen pixel width (optional transparent background). Sources are never overwritten.
- **Fit Page Guide to Content**: sizes the optional print guide to the current
  arrangement (export framing; Workspace remains ad-hoc, not paper-first).

## License

GPL-3.0-or-later. See [LICENSES](LICENSES) and [REUSE.md](REUSE.md).

## Links

- GitHub: <https://github.com/Grumbel/qimgview>
- Radicle: [`rad:z3BEnqZd8JN1DNMPEuLPv5ACgzq3a`](https://radicle.network/nodes/rosa.radicle.network/rad:z3BEnqZd8JN1DNMPEuLPv5ACgzq3a)
