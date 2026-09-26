# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2710.2-ocr-baduri-path` (base `b65f69e`).

OCR panel log includes full session path on start/fail; BadUri shows path.
Needs thumtoo **343.6** for EPUB page OCR (`//epub:…//page:N`).

### Apply
```bash
git pull --ff-only …/thumtoo-343.6-ocr-epub-pages-66fc03e.bundle HEAD
git pull --ff-only …/biltoo-2710.2-ocr-baduri-path-b65f69e.bundle HEAD
```

---

## Roadmap / later

### 0.3.0 — Dock layout persistence (evaluate KDDockWidgets)

**Problem:** User panel positions should stick across runs. Stock
`QMainWindow::saveState` / `restoreState` is the official Qt API and is what
most apps use, but `QDockAreaLayout` has known sharp edges (timing, multi-monitor
floating geometry, occasional SEGV). Biltoo currently avoids the binary blob
and only persists some explicit visibility keys + readable window geometry.

**Option A (smaller):** Careful `saveState`/`restoreState` again — unique
`objectName` on every dock, restore only after all docks exist (optionally
`QTimer::singleShot(0, …)`), version the blob, ignore failed restore.

**Option B (0.3.0 candidate):** [KDDockWidgets](https://github.com/KDAB/KDDockWidgets)
(nixpkgs: `kddockwidgets`, Qt6 build ~2.4.x, GPL-2/GPL-3). Purpose-built
docking + **`LayoutSaver`** (JSON/file) instead of Qt’s opaque state. Not a
drop-in: replace `QMainWindow`/`QDockWidget` with KDDW’s types, rewire View
menu toggles, migrate all docks (filmstrip, metadata, layout, crop, OCR, text,
help, adjustments, TOC, …).

**Suggested path for 0.3.0:**

1. Spike: flake `kddockwidgets` + minimal MainWindow with 2–3 docks + LayoutSaver
   round-trip.
2. Decide keep stock Qt docks vs full KDDW migration.
3. Only then move the full shell.

Do **not** attempt a wholesale dock rewrite in a single tip without the spike.
