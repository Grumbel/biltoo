# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2725.1-drop-with-thumtoo-flag` (base `037c98f`).

### 2725.1 — Remove obsolete BILTOO_WITH_THUMTOO
- thumtoo is hard-required via `THUMTOO_SOURCE_DIR` only.
- Dropped unused `-DBILTOO_WITH_THUMTOO=ON` from flake.nix / default.nix
  (fixes CMake unused-cli warning).

### Apply
```bash
git pull --ff-only …/biltoo-2725.1-drop-with-thumtoo-flag-037c98f.bundle HEAD
```
