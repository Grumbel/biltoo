# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2709.1-text-panel` (base `b65f69e`).

### 2709.1 — Text panel + glyph overlay
- View → **Show Text Panel**: region list, selection/hover ↔ page
- **Show text in boxes** (glyphs) and outlines checkboxes
- `TextLayerController` is QObject (`layerChanged` / `selectionChanged` / `hoverChanged`)
- `TextPanelModel`, `TextOverlayState`, [docs/TEXT_OVERLAY.md](docs/TEXT_OVERLAY.md)

Needs thumtoo **343.3+** for OCR layers on plain images.

### Open
- Page→panel hover from mouse over bboxes
- Multi-page range in Text panel

### Apply
```bash
git pull --ff-only …/biltoo-2709.1-text-panel-b65f69e.bundle HEAD
```
