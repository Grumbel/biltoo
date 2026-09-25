# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2656.2-even-odd-batch-targets` (base `9740316`).

### Done
- BatchTargets for crop, colour, orient (2653–2655).
- **2656.1**
  - `BatchTargets::EvenIndices` / `OddIndices` (session list parity).
  - Crop + Adjustments panels expose Even / Odd in Targets.
  - Reset content appearance returns correct non-live clear count.

### Verify (agent)
- Static symbol + enum + panel wiring checks: **pass**.
- Full compile not possible in this sandbox (no nix/Qt). Host must build.

### Host smoke
1. `git pull` this bundle; `cmake --build` / `nix build`.
2. Filmstrip multi-select → Flip / Colour Apply / Crop Apply.
3. Targets → Even indices → Apply crop/colour across session.

### Next
- Template-from-page crop recipe (optional product).
- Stack/sum preview (later).

### Apply
```bash
git pull --ff-only …/biltoo-2656.2-even-odd-batch-targets-9740316.bundle HEAD
```
