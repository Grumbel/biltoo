# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2843.3-visualrect-index (on origin `94a711d`).

### Bundle stacking rule
Next tip base = current origin/master (or previous tip after pull). Never
re-root the same fixes on an older origin commit. Fix-forward only.

### 2843.3
- `visualRect(indexFromItem(hit))` — Qt6 QListView API (compile fix).

### On origin already
- 94a711d OCR toolbar opens panel
- c3ff7f9 open-focus double-click (broken visualRect)
- 6c3a0cc slideshow min_scale 0
