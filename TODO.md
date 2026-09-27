# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2721.1-tts-highlight-vs-selection` (base `86c938c`).

### 2721.1 — TTS highlight with active selection
- Root cause: Speak used full-page `buildSpeakPlan(true)` but highlight mapped
  via `speakSpans()` → `buildSpeakPlan(false)`, which shrinks to the selection
  so sentence offsets no longer match audio.
- `speakableText` / `speakSpans` always use the full-page plan; mainwindow
  highlight path uses `buildSpeakPlan(true)` explicitly.

### Apply
```bash
git pull --ff-only …/biltoo-2721.1-tts-highlight-vs-selection-86c938c.bundle HEAD
```
