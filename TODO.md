# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.7-virtual-tiles-not-emb` (base `bcbb97e`).

### 2714.7 — Fast Gallery scroll: tiles not EMB when warm
- Virtual slots painted EMB/LQIP even with process tile RAM — product is tiles
  once warm (GALLERY_PIXELS / KILL_SOFT).
- `paintVirtualPlaceholders`: if `TileLodRegistry` has succeeded tiles, paint
  via `prepare_and_paint_cover` (no issue); drop EMB when durable tiles known.
- **GLOSSARY.md**: Display samples section (Tiles / LQIP / EMB / warm / cold /
  virtual slot) + do-not-confuse pairs.

### Prior (included)
2714.6 orient · 2714.5 overlay · 2714.4 viewport · 2714.3–1 F5/tiles

### Apply
```bash
git pull --ff-only …/biltoo-2714.7-virtual-tiles-not-emb-bcbb97e.bundle HEAD
```
