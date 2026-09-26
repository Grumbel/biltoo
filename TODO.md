# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2703.4-no-placeholder-sizes` (base `6c3e877`).

### 2703.4 — Real size or nothing
No provisional / 1×1 / 256² stand-in sizes for geometry:
- `markFailed`: failure flag only, no invented size
- Virtual plan: definitive sizes only (skip failed / provisional)
- `createPlaceholderItem`: reject size ≤ 1 in all modes
- `imageSizeForPath` / `layoutSizeForPath`: return {} and probe, never markProvisional
- `createItemFromImage`: nullptr until definitive size
- Failed probe: drop live cells, no tooltip 256 cell

### 2703.3 — cold sizeReady book + no dual pack during gate
### 2703.2 — Workspace delete BSP
### 2703.1 — Soft F5 size book

### Apply
```bash
git pull --ff-only …/biltoo-2703.4-no-placeholder-sizes-6c3e877.bundle HEAD
```
