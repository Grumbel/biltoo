# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2759.1-kddockwidgets` (base `9395b3e`).

### 2759.1 — KDDockWidgets panel shell (first cut)
- Required dep: `kddockwidgets` (Nix) + `find_package(KDDockWidgets-qt6)`
- `MainWindow` : `KDDockWidgets::QtWidgets::MainWindow`
- All tool docks → `QtWidgets::DockWidget` (`open`/`close`/`isOpen`/`toggleAction`)
- Layout: `LayoutSaver` serialize/restore; `dockLayoutVersion` = 2
- `initFrontend(QtWidgets)` in `main`
- docs/KDDOCK.md mapping notes
- Filmstrip edge uses KD `Location_*`; no Qt `resizeDocks`

**Expect compile feedback** on exact include paths / signal names
(`isOpenChanged` vs KD version) under your Nix Qt6 tree.

### Apply
```bash
git pull --ff-only …/biltoo-2759.1-kddockwidgets-9395b3e.bundle HEAD
# nix develop / biltoo-configure — needs pkgs.kddockwidgets on PATH
```
