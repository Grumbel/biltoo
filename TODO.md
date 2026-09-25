# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2678.2-research-tile-overlap` (base `932ed5c`).
Stack on origin: **2677** (smooth 1×/2×) + **2678** (research doc only).

### Research (do not implement until reviewed)
**257 / lower-zoom missing pieces** — full write-up:
[docs/RESEARCH_TILE_OVERLAP.md](docs/RESEARCH_TILE_OVERLAP.md)

**Headline findings**
1. Interior ExactTile expand for 257 payloads matches thumtoo (scale 0/1/2 OK).
2. **Real coverage bug:** `tile_content_rect` uses step `256×2^s` while level size
   is floor-half → odd widths leave **1–N content pixels uncovered** at scale>0
   (e.g. 513px image at scale 1 covers only 512). Fits “missing pieces at lower
   zoom” especially on right/bottom edges.
3. Overdraw `0.75/dpc` becomes **many content pixels** when zoomed out (dpc=0.1
   → ±7.5px) — can look like lines are “eaten”; separate from overlap expand.
4. JPEG dual-encode of the shared strip remains a visual residual.
5. CoarserTile dest expand was a prior bug (fixed in 8e62ea3); parent UV mapping
   into exclusive 256 is intentional.

**Next (after human review):** fix coverage mapping and/or clamp overdraw; add
odd-width coverage tests. Do not “tweak 257 expand” without reading the research.

### Prior in this stack
- **2677:** `tilePaintNeedsSmooth` only skips at true 1:1 / 2:1 density (not 4700%).
- **2676** (on origin before this stack): seam paint order + overdraw; ExactTile +1 expand.

### Apply
```bash
git pull --ff-only …/biltoo-2678.2-research-tile-overlap-932ed5c.bundle HEAD
```
