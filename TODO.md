# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2650.1-autocrop-wait-samples` (base `9740316`).

### Done
- Crop panel + multi-apply stack through 2649.3.
- **2650.1 Autocrop no longer skips blank tiles:** before planning, loads
  soft (≤512) samples off-GUI into a **private** map (avoids ImageCache hash
  races). `QProgressDialog` + centre HUD while waiting; optional Cancel.
  Soft resolution is enough for margin detect. Apply phase has its own
  progress for large N. Flash reports applied vs still-skipped counts.

### Next
1. Crop panel polish: live preview; normalised fields.
2. Template / even-odd / stack (later).

### Apply
```bash
git pull --ff-only …/biltoo-2650.1-autocrop-wait-samples-9740316.bundle HEAD
```
