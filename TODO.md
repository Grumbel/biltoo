# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2655.1-orient-batch-targets` (base `9740316`).

### Done
- Crop + colour BatchTargets (2653–2654).
- **2655.1 Orient via BatchTargets**
  - ImageView `setFilmstripSelectionProvider` (MainWindow binds filmstrip).
  - `BatchTargets::resolve(Selection)` always unions filmstrip selection.
  - Flip H/V, rotate L/R, reset content appearance use expanded targets;
    non-live ids get ItemWorld orient + `pushSessionContentCommand`.

### Verify
- Full `nix build` not available in agent sandbox (no nix / Qt). Static
  symbol check passed for batch APIs. Host should `cmake --build` or
  `nix build` before relying on runtime.

### Next
1. Template / even-odd / stack (later product).
2. Runtime smoke: filmstrip multi-select → flip/colour/crop without live tiles.

### Apply
```bash
git pull --ff-only …/biltoo-2655.1-orient-batch-targets-9740316.bundle HEAD
```
