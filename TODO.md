# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2754.1-leptonica-pkg-config` (base `9395b3e`).

### Done this tip
- `default.nix`: `leptonica` so tesseract.pc Requires: lept is on
  PKG_CONFIG_PATH (silence "Package 'lept' was not found")
- CMake OCR-disabled warning mentions leptonica
- Pair with thumtoo-348.1 (mkBuildInputs leptonica)

### Prior
- 2753.1: disconnect filmstrip chrome scroll slots on ~MainWindow
- 2752.1: chrome colour prefs + cyan Select

### Apply
```bash
git pull --ff-only …/biltoo-2754.1-leptonica-pkg-config-9395b3e.bundle HEAD
```
