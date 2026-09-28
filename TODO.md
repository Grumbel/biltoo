# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2783.1-fix-annotation-compile` (base `a989daf`).

### 2783.1
- Fix annotationcontroller.cpp: remove duplicate `painter.restore()` / `}` that
  broke the draft-tool paint block (brace imbalance → “expected declaration
  before '}'”).
- Fix mainwindow_print.cpp: include `imageitem.h` so `ImageItem::path()` is
  complete in `exportAnnotatedPng()`; rename inner `image` → `imageMode` to
  clear -Wshadow.

### Prior tip
`biltoo-2782.2-sticky-edit-verify` (base `b8a0cf3`) — sticky double-click edit.

### Apply
```bash
git pull --ff-only …/biltoo-2783.1-fix-annotation-compile-a989daf.bundle HEAD
```
