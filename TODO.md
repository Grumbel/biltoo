# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2773.2-annot-paint-overlay-ref` (base `b8a0cf3`).

### Stack
… 2773.1 text highlight + undo → **2773.2** fix paintOverlay(QPainter*) call

### 2773.2
- `ViewShellChrome::paintForeground`: pass `*painter` to `paintOverlay(QPainter&)`
  after null check (compile fix)

### Apply
```bash
git pull --ff-only …/biltoo-2773.2-annot-paint-overlay-ref-b8a0cf3.bundle HEAD
```
