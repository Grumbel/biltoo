# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2649.3-crop-panel-qpointer-fix` (base `9740316`).

### Done
- Crop panel v1 + autocrop batch hardening (2649.1–.2).
- **2649.3:** `ImageItem` is `QGraphicsPixmapItem`, not `QObject` — drop
  `QPointer<ImageItem>` (compile error); keep raw pointers + plan phase.

### Next
1. Crop panel polish: live preview; normalised fields; status when size unknown.
2. Template / even-odd / stack (later).

### Apply
```bash
git pull --ff-only …/biltoo-2649.3-crop-panel-qpointer-fix-9740316.bundle HEAD
```
