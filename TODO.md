# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2663.1-open-selection-content-snap` (base `2e49220`).
**Thumtoo:** flake.lock pins `5e47314` (//pdfimage Client + cmake summary align).

### This tip
- Open Selection snapshots: transfer **content** appearance only
  (`hasContentEditComponents` / attention), not Workspace pose-only durable.
- Pose-only no longer skips `freezeItemAppearance` (committed crop path).
- Docs: SRC layout already landed for 0.2.0; §4.5 matrix updated.

### Pre-0.2.0 remaining (human RC)
- Smoke: crop Apply → Gallery select → Open Selection in New Window keeps crop.
- Smoke matrix in docs/RELEASE_0.2.0.md §3 (reorder, export, PDF pages, shortcuts).
- Tag: VERSION → 0.2.0 when RC green (not this tip).

### Apply
```bash
git pull --ff-only …/biltoo-2663.1-open-selection-content-snap-2e49220.bundle HEAD
```
