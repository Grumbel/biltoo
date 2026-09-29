# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2830.1-tiles-only-warm on 2829.2 stack.

### Policy (authoritative)
- **Warm** (durable tiles): TileSynth whole-frame / interactive tiles — never soft PreferCache encode
- **Cold**: size probe + EMB (free) + placeholder + interactive tiles when issued — **no** soft encode, **no** LQIP generation
- **LQIP**: warm only (side-effect of tile work per THUMTOO_HOST_CONTRACT)

### 2830.1
- scheduleDisplayPixels requires hasDurableTilesKnown
- PathRaster / filmstrip cold → probe only
- Work log: TileSynth only
- GALLERY_PIXELS aligned

### Required thumtoo
thumtoo-008.1-markdown-cmark-mutex-3e6987f.bundle
