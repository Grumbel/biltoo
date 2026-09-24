# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2470.1-fix-ownership-compile** (base `7d823d8`).

### Fix compile after ownership moves
- `pageGuideRectFromHandleDrag` declared on WorkspaceController
- ImageController: `#include <QSize>`, `itemHasReliableFrameSize` helper
- `refreshScrollBarGeometry` public host API for ImageController framing

### Apply
```bash
git pull --ff-only …/biltoo-2470.1-fix-ownership-compile-7d823d8.bundle HEAD
```
