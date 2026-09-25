# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2658.1-batch-crop-tests` (base `9740316`).

### Done
- **2658.1 Unit tests**
  - `croprecipe` — manual margins, clamping, extra expand, autocrop (stub trim)
  - `batchtargets` — pure `sessionIndices` (range / even / odd)
  - `contentundomacro` — single vs multi undo grouping
  - `BatchTargets::sessionIndices` inlined in header; resolve uses it

### Host verify
```bash
ctest -R 'croprecipe|batchtargets|contentundomacro' --output-on-failure
```

### Apply
```bash
git pull --ff-only …/biltoo-2658.1-batch-crop-tests-9740316.bundle HEAD
```
