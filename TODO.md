# TODO / agent handoff

## Status (2026-09-25)

**Tip: biltoo-2641.1-gallery-always-on-bars** (base `1e40934`).

### GUI_BUDGET spam
Exceed logs are silent unless `BILTOO_GUI_BUDGET_LOG=1` (or STRICT).

### Gallery “off centre” / covered edge without H-bar
Layout packs left→right from margin into `availW`. With **AsNeeded**, that
`availW` was the full client; a vertical bar then covers the right edge of the
pack while scene width stays ≈ client → **no horizontal scrollbar**, content
looks shifted/clipped under the bar. Image mode centres a single underlay and
does not hit this.

**Fix:** when scrollbars are enabled, Gallery uses **AlwaysOn** (viewport already
excludes gutters); pack measures live viewport only. Image/Workspace stay AsNeeded.

### Apply
```bash
git pull --ff-only …/biltoo-2641.1-gallery-always-on-bars-1e40934.bundle HEAD
```
