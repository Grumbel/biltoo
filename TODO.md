# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2774.2-fix-qBound-order` (base `b8a0cf3`).

### 2774.2
- Fix SIGABRT in freehand stroke simplify: `qBound(value, min, max)` not `(min, max, value)`

### Apply
```bash
git pull --ff-only …/biltoo-2774.2-fix-qBound-order-b8a0cf3.bundle HEAD
```
