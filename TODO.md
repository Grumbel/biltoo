# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2463.1-fix-dual-timer-null-warnings** (base `7d823d8`).

### Fix
- Quiet `-Wnull-dereference` in DualImageShell open retry lambda (QPointer
  → raw pointer after null check).

### Prior dual tips still in stack
- 2462: Ctrl+Shift+2, software secondary viewport, soft seed
- 2461: per-surface pipeline
- 2460: shared size book

### Apply
```bash
git pull --ff-only …/biltoo-2463.1-fix-dual-timer-null-warnings-7d823d8.bundle HEAD
```
