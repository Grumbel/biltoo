# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2735.1-spread-gallery-tts-ring` (base `636e70e`).

### Spread stack — P0–P3 complete

| Phase | Status |
|-------|--------|
| P0–P1 reading surface | complete |
| P2 text select/copy/panel/search/glyphs | complete |
| P3 TTS + Gallery ring | complete (2734.1 + 2735.1) |

### 2735.1
- `GalleryController::setSpeechHighlightPaths` — all spread members ringed
- Active member strong green; companions dim
- Speak starts with member path list; sentence progress updates active path

### Still open (P4)
- Binding-hint UI (CoverAlone / StrictPairs toggle)
- RTL / vertical spread direction
- Max-N UI preference

### Apply
```bash
git pull --ff-only …/biltoo-2735.1-spread-gallery-tts-ring-636e70e.bundle HEAD
```
