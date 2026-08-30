# QImgView

A classic Qt image viewer with **Gallery** layouts for browsing a session and an optional free-form **Workspace** for comparing images side by side.

## Project home

- GitHub: <https://github.com/Grumbel/qimgview>
- Radicle: `rad:z3BEnqZd8JN1DNMPEuLPv5ACgzq3a`

## Features

- Fast single-image browsing with zoom, pan, rotate, and flip
- Thumbnail bar for the current session; open files or directories from the UI or the command line
- **Gallery**: packaged layouts (horizontal/vertical strip, grid, grid-crop, masonry, stack); click an image to view it full size, **Up** to return
- **Workspace mode**: free-form canvas — move, scale, rotate, opacity, and stacking order
- Slideshow with optional fullscreen
- Metadata panel (file info; optional Exif/IPTC/XMP when built with libexiv2)
- Configurable canvas background (solid or checkerboard)
- Theme icons, fullscreen, and preferences for sort order, slideshow, and default applications

## Usage

```bash
qimgview [options] [files-or-directories…]
```

Useful options:

| Option | Description |
|--------|-------------|
| `-r`, `--recursive` | Expand directories recursively |
| `--sort=name\|mtime` | Sort order for loaded files |
| `--slideshow` | Start the slideshow |
| `--interval=ms` | Slideshow interval in milliseconds |
| `--thumbnails` / `--no-thumbnails` | Force thumbnail bar on or off |

Drag and drop image files onto the window. In normal mode a plain drop replaces the session; Shift or Ctrl+drop appends. In workspace mode, drops add images to the session and the canvas.

## Building

**Nix** (recommended):

```bash
nix develop    # development shell
nix build      # package
nix run        # build and run
```

**Manual** (Qt6 Widgets, CMake; optional libvips, libexiv2, GLib GIO):

```bash
mkdir build && cd build
cmake ..
cmake --build .
./qimgview
```

## Desktop integration

The package installs:

- `share/applications/qimgview.desktop`
- `share/icons/hicolor/scalable/apps/qimgview.svg`

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [REUSE.md](REUSE.md).
