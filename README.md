# QImgView

**QImgView** is a classic Qt 6 desktop image viewer with three modes on one canvas:

| Mode | Purpose |
|------|---------|
| **Image** | Browse one file at a time — zoom, pan, rotate, flip, crop, slideshow |
| **Gallery** | See the whole session in packaged layouts (strip, grid, masonry, …) |
| **Workspace** | Arrange several images freely for comparison, markup framing, and export |

![QImgView main window](screenshot.png)

## Features

### Session & files

- Open files, directories, or **archives** (zip, tar variants, 7z, rar) via File dialogs, drag-and-drop, or the command line
- **File → Open** replaces the session; **File → Add** and drops **append**
- **Recent Sessions** and **Recent Projects** menus
- Sort by name, date, size, width, height, or pixel count
- Thumbnail bar with per-mode visibility defaults, edge placement, labels, optional square crop

### Image mode

- Zoom: in/out, 1:1, fit, fill, rubber-band region (**Z**)
- Pan; rotate ±90°; flip H/V (session transforms until export)
- **Slideshow**: Space to start/stop; `[` / `]` change dwell; optional fullscreen
- Transitions (Preferences): none, crossfade, fade through black, slide projector
- Optional **Ken Burns** pan/zoom during each dwell (cover framing + gentle drift)
- Edge navigation; HUD with optional dwell progress line

### Gallery

- Layouts: side-by-side, vertical strip, grid, masonry (columns or rows), masonry fill
- Multi-select; double-click or Enter opens Image mode
- Return restores the previous Gallery viewport
- Grid-crop layout remains disabled until it coexists cleanly with session crop

### Workspace

- Free placement: move, scale, rotate, shear, opacity, raise/lower
- Tools: **Select**, **Pan**, **Zoom** (region) on a left toolbar
- Durable canvas: leaving for Gallery/Image and returning restores the snapshot
- **Layout panel**: pack the current selection without leaving Workspace
- **Page Guide** and **Fit Page Guide to Content** for export framing
- Custom **Workspace background** (solid, checker, image tile) with temporary Default preview
- Delete removes from the canvas only; session membership stays
- Open the current selection in a new window

### Crop & appearance

- Crop in Image mode or on a single Workspace selection (rotatable draft, expand/pad, modifiers)
- Colour **Adjustments** dock (opt-in); metadata side panel
- Non-destructive session/project appearance until you export

### Print & export

- Page Setup, Print, Print Preview, **Export PDF**
- **Export PNG** at a chosen width (content bounds or page guide; optional transparency)
- Sources are never overwritten by export

### Projects

- **Open/Save Project** (`.qimgview`): session ids, appearance, Workspace poses, SHA-256 content addressing for external files

## Keyboard shortcuts

Press **F1** in the app for the full list. Highlights:

| Keys | Action |
|------|--------|
| ← / → | Previous / next |
| Space | Slideshow start/stop |
| `[` / `]` | Slideshow slower / faster |
| F / F11 | Fullscreen |
| H | HUD on/off |
| Ctrl+0 / + / − | 1:1 / zoom in / out |
| Z | Zoom to region (one-shot) |
| C | Crop |
| R | Rotate right |
| Ctrl+O / Ctrl+S | Open / save project |
| Ctrl+Q | Quit |

## Command line

```bash
qimgview [options] [files-or-directories…]
```

| Option | Description |
|--------|-------------|
| `-r`, `--recursive` | Expand directories recursively |
| `--start-at=N` | Start at the *N*-th image (1-based) |
| `--sort=name\|mtime` | Sort by name or modification time |
| `--mode=image\|gallery\|workspace` | Initial mode |
| `--slideshow` | Start slideshow after loading |
| `--interval=ms` | Slideshow dwell in milliseconds |
| `-f`, `--fullscreen` | Start fullscreen |
| `--thumbnails` / `--no-thumbnails` | Force thumbnail bar on or off |

Drag-and-drop onto the window **appends** to the session.

## Image formats

Common formats use Qt. Extra types (e.g. GIMP `.xcf` and related plugins) work when **KImageFormats** is available — the Nix package includes it. Archives are expanded when **libarchive** is linked at build time.

## Desktop integration

Installed files (CMake / Nix):

| File | Location |
|------|----------|
| `qimgview.desktop` | `$prefix/share/applications/` |
| `qimgview.metainfo.xml` | `$prefix/share/metainfo/` (AppStream) |
| `qimgview.svg` | `$prefix/share/icons/hicolor/scalable/apps/` |

The `.desktop` entry registers common image MIME types and popular archive types so file managers can open them with QImgView.

## Build / install

### Nix

```bash
nix run github:Grumbel/qimgview
# from a checkout:
nix develop    # shell
nix build      # package
nix run        # run
```

### CMake (Qt 6 Widgets)

```bash
mkdir build && cd build
cmake ..
cmake --build .
cmake --install .   # optional; installs binary, desktop, metainfo, icon
./qimgview
```

Optional libraries (vips, exiv2, libarchive, …) are detected when present. CMake tests include `qimgview --help` and project-file round-trip unit tests.

## License

GPL-3.0-or-later. See [LICENSES](LICENSES) and [REUSE.md](REUSE.md).

## Links

- GitHub: <https://github.com/Grumbel/qimgview>
- Radicle: [`rad:z3BEnqZd8JN1DNMPEuLPv5ACgzq3a`](https://radicle.network/nodes/rosa.radicle.network/rad:z3BEnqZd8JN1DNMPEuLPv5ACgzq3a)

## Developer docs

In-tree notes for contributors: [AGENTS.md](AGENTS.md), [DOMAIN.md](DOMAIN.md), [SESSION.md](SESSION.md), [TODO.md](TODO.md).
