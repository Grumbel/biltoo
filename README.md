# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# QImgView

A classic Qt image viewer that treats the image area as a workspace.

## Features (planned / in progress)

- Toolbar inspired by Ristretto (Open, Zoom ±, 1:1, Fit, Fullscreen, Rotate L/R)
- Single image or groups of images (command line or drag-and-drop)
- Thumbnail panel when multiple images are loaded
- Free rotation and continuous zoom
- Optional Exif side panel
- Rich command-line interface

See [TODO.md](TODO.md) for the full roadmap.

## Building

This project uses a Nix flake for a reproducible environment.

```bash
nix develop          # enter development shell
nix build            # build the package
nix run              # build and run
```

Manual build (with Qt6 and CMake available):

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

The same SVG is embedded as a fallback window icon when the theme icon is not available.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and the SPDX headers in source files.
This project follows the [REUSE specification](https://reuse.software/).
