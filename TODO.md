# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2884.1-session-gallery-layout (linear stack on `e345338`).

### 2884.1
- Gallery **layout mode** and **columns/rows** live on `SessionDocument` and
  persist in `.biltoo` project JSON under `gallery: { layoutMode, masonryColumns,
  gridColumns, masonryRows }`. Runtime changes (layout menu, Columns spin)
  update the session; project load restores them before entering Gallery.

### 2883.1
- TTS full document + selection seek while speaking.

### 2882.1 / 2881.*
- Spread equal-height centre; pipewire; Gallery↔Image latency doc.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
