# TODO / agent handoff

## Status (2026-09-28) — **verified**

**Tip:** `biltoo-2740.2-text-layer-leave-doubleview` (base `636e70e`).

### Spread / Double View — complete through 2740.2

| Area | Status |
|------|--------|
| P0–P4 surface (membership, text, TTS, binding/RTL/N) | done |
| Multi-underlay install | done |
| Toolbar split button + Back | done |
| Sticky FixedN across Gallery | done |
| Zoom preserve (fit only on membership change) | done |
| Prune stale underlays on Prev/Next | done |
| Center (AlignCenter + fitInView) | done |
| Toggle does not force Image from Gallery | done |
| Leave Double View clears multi underlays | done |
| Text overlays per underlay (no primary-layer leak) | done |
| CMake SYSTEM includes for Qt | done |
| TextSelection metatype outside guard | fixed |

### Static verification (2740.2)
- `layerForItem` returns nullptr unless sid/path match; primary layer only for `layerPath`
- Leave path: `clearLiveCanvas` + `loadImage`
- `applySpreadLayout` prunes non-members; `AlignCenter`
- Prev/Next pass `forceFit=true`
- `clearLiveCanvas` destroys all live items (stashes protected)

### Residual risk (runtime QA)
- First layout before soft sizes resolve may need a second fit when HQ arrives
- Selection-policy spreads still clear on single-tile open (intentional)

### Apply
```bash
git pull --ff-only …/biltoo-2740.2-text-layer-leave-doubleview-636e70e.bundle HEAD
```

No further design work queued; next step is interactive QA on a full Qt build.
