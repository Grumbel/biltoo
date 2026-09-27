# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2724.1-tts-highlight-page-bound` (base `795a278`).

### 2724.1 — TTS highlight only on the spoken page
- Capture SpeakPlan path + spans at Speak; map highlights only when
  `classicPath() == m_ttsSpeakPath`.
- Switching pages clears green boxes on the foreign page (speech continues).

### Apply
```bash
git pull --ff-only …/biltoo-2724.1-tts-highlight-page-bound-795a278.bundle HEAD
```
