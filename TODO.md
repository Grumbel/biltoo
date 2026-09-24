# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2474.2-fix-sizeresolve-qpointer** (base `7d823d8`).

### Fix compile after 2474
- `gallerycontroller_sizeresolve.cpp`: `#include <QPointer>` (incomplete type on QPointer<ImageView>)
- ImageView ctor: init-list order matches declaration (`m_gallery` before `m_size`)

### Stack
2471–2474 ownership · **2474.2** compile fix

### Apply
```bash
git pull --ff-only …/biltoo-2474.2-fix-sizeresolve-qpointer-7d823d8.bundle HEAD
```
