<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Portability (Linux → Windows and beyond)

Status notes for a future Windows build or cross-compile. The **application
logic** is mostly portable Qt 6; **packaging and a few optional features** are
Linux-oriented. A Windows build is realistic; a drop-in cross-compile from the
current Nix flake is not.

## Summary

| Question | Answer |
|----------|--------|
| How many hard Linuxisms in core code? | Few — mostly optional or packaging |
| Native Windows build (MSVC/MinGW + Qt 6 + CMake)? | Feasible |
| Cross-compile from the Nix flake as-is? | Not defined; would need a separate toolchain path |
| Rewrite required? | No — build, deps, and deploy work |

## Linux-specific or Linux-leaning pieces

| Area | Severity | Notes |
|------|----------|--------|
| **GIO / “default application”** (`src/defaultapps.cpp`) | Real Linuxism | Uses `gio-unix-2.0` / `GDesktopAppInfo`. Already gated with `QIMGVIEW_HAVE_GIO`. On Windows the feature simply is not built; no need to port unless someone wants a native “set default app” UI later. |
| **Desktop integration** (`data/qimgview.desktop`, AppStream metainfo) | Packaging only | FreeDesktop / Linux store metadata. Irrelevant on Windows (use a different installer story). |
| **Nix** (`flake.nix`, `default.nix`, `wrapQtAppsHook`) | Dev / packaging | Primary development environment on Linux. Not required to build the app; **CMake** is the portable entry point. |
| **Icon theme names** (`QIcon::fromTheme`, names like `document-open`) | Soft | Common on Linux. On Windows, code already falls back to `QStyle::SP_*` and bundled icons (`src/icons.cpp`). UI may look plainer without a FreeDesktop icon theme unless icons are shipped. |
| **Optional native libraries via pkg-config** (libvips, exiv2, libarchive) | Soft | CMake enables them when found (`QIMGVIEW_HAVE_VIPS`, `QIMGVIEW_HAVE_EXIV2`, `QIMGVIEW_HAVE_ARCHIVE`). Without them, Qt image codecs still work; archive-in-session and extra formats/metadata degrade gracefully. On Windows use vcpkg, MSYS2, or ship without the optionals. |
| **Path examples / tests** | Cosmetic | Docs and some tests use Unix-style paths (`/tmp/...`). Runtime I/O goes through Qt (`QFile`, `QUrl::fromLocalFile`). Archive member refs use a `//archive:` marker with forward slashes — keep that convention on Windows rather than mixing backslashes into the ref syntax. |

## Portable core

- UI and viewer: **Qt 6** (Core, Gui, Widgets, PrintSupport).
- Build: **CMake** with C++17, AUTOMOC/AUTORCC. MSVC warning flags (`/W4`) are already branched in `CMakeLists.txt`.
- No heavy use of raw POSIX (`fork`, `mmap`, etc.) in the main application path.
- Optional features fail closed when the corresponding library is absent.

## Windows build options

### 1. Native Windows build (recommended first)

- MSVC or MinGW + Qt 6 + CMake.
- Optional deps via vcpkg or MSYS2 if desired.
- GIO remains off (`QIMGVIEW_HAVE_GIO` unset).

### 2. Linux → Windows cross-compile

- Possible with a MinGW Qt toolchain, but requires maintaining sysroots, plugin deployment, and optional native libraries.
- The **Nix flake does not define this path** today.

### 3. Shipping checklist (beyond “it links”)

- Deploy Qt plugins (platforms, imageformats, printsupport) — e.g. `windeployqt`.
- Installer or portable layout.
- Path/case testing: drive letters, mixed separators; archive refs still use the project’s `//archive:` form.
- Shortcut and menu habits (some chords already considered Windows conventions, e.g. Ctrl+Y).
- Do not rely on XDG icon themes unless an icon set is shipped; prefer bundled icons + style fallbacks.

## Suggested future work (not scheduled)

- [ ] Document a minimal Windows CMake command line in this file or README once smoke-tested.
- [ ] CI job (GitHub Actions `windows-latest`) for Release builds without optional deps.
- [ ] Optional: Windows “default app” via registry / Settings APIs (separate from GIO).
- [ ] Optional: MinGW cross section in the flake (only if maintainers want it).

## Related files

- `CMakeLists.txt` — feature probes and install rules  
- `src/defaultapps.cpp` — GIO-gated default-app helpers  
- `src/icons.cpp` — theme icon + style fallback  
- `src/archivereader.cpp` / `src/imageloader.cpp` — optional libarchive / libvips  
- `flake.nix` / `default.nix` — Linux packaging only  
