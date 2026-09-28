# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2753.1-fix-filmstrip-chrome-dtor` (base `9395b3e`).

### Done this tip
- Shutdown crash: `ImageView::~ImageView` → `setScene(nullptr)` → scrollbar
  `valueChanged` → `MainWindow::scheduleFilmstripChromeUpdate` after
  `~MainWindow` body finished (`assertObjectType<MainWindow>`). Disconnect
  scrollbars + stop filmstrip chrome timer in `~MainWindow`.

### Prior
- 2752.1: configurable chrome colours; brighter cyan Select
- 2751.1: filmstrip camera on scroll
- 2750.x / 2749.1: ChromeColors + cursor members

### Apply
```bash
git pull --ff-only …/biltoo-2753.1-fix-filmstrip-chrome-dtor-9395b3e.bundle HEAD
```
