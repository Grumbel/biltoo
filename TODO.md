# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2734.1-spread-p3-tts-glyphs` (base `636e70e`).

### Spread stack
| Phase | Status |
|-------|--------|
| P0–P1 reading surface | complete |
| P2 text select/copy/panel/search | complete |
| P3 TTS + secondary glyphs | 2734.1 |

### 2734.1
- `SpeakSpan` carries `sessionId`; `buildSpeakPlan` joins all spread members
- Speaking highlight targets the active member underlay
- Glyph paint on every spread underlay
- Speak uses full-spread plan; multiSelection anchors start sentence

### Still open
- Gallery TTS ring for all spread members while speaking
- Binding-hint UI / RTL (P4)

### Apply
```bash
git pull --ff-only …/biltoo-2734.1-spread-p3-tts-glyphs-636e70e.bundle HEAD
```
