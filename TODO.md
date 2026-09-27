# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.11-unified-tile-display` (base `bcbb97e`).

### Tile pixels — one path

| API | Use |
|-----|-----|
| `TileLodRegistry` | Process-wide path-keyed Succeeded tiles (shared all modes/widgets) |
| `paint_tiles_display` | Oriented paint (Image/Gallery/Workspace `ImageItem`) + plan overlay |
| `prepare_and_paint_cover` | Identity native→dest cover (Slideshow) via same registry |
| `paint_draw_plan` | Low-level plan iterator (identity controller paint) |

Filmstrip still uses **TileSynth** whole-frame into ImageCache for strip-edge
icons (same durable Store, not grid paint). Canvas modes share one registry.

### Apply
```bash
git pull --ff-only …/biltoo-2714.11-unified-tile-display-bcbb97e.bundle HEAD
```
