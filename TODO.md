# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2812.1-mupdf-pin-from-thumtoo` (base `2085c07`).

### 2812.1
- Pin MuPDF 1.28.5 in biltoo flake (prefer `thumtoo.lib.pinMupdf`, else local override)
- Pass pinned mupdf into `default.nix` and `mkBuildInputs` via `pkgsForThumtoo`
- So `THUMTOO_SOURCE_DIR=… nix develop -c biltoo-test` links 1.28.5, not nixpkgs 1.27.2

### Prior
- Tool unification 2811.x; Text Highlighter design note 2811.7

### Next
- After rebuild: `pkg-config --modversion mupdf` → 1.28.5 in biltoo shell
- Optional: `nix flake update thumtoo` once 353.x is on the remote

### Apply
```bash
git pull --ff-only …/biltoo-2812.1-mupdf-pin-from-thumtoo-2085c07.bundle HEAD
```
