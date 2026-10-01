# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2886.1-tts-no-auto-page-follow (linear stack on `e345338`).

### 2886.1
- TTS: do **not** auto-turn pages while speaking (optional). Text panel
  checkbox **Turn pages while speaking** + `speech/followPages` (default
  **false**). Gated `setCurrentIndex` on `sentenceStarted`.
- TTS: selection change while speaking seeks only when
  `speakAnchorOffset` finds a matching span (returns −1 for empty/unmatched —
  no more jump to document start / first page).
- Docs: [docs/TEXT_TO_SPEECH.md](docs/TEXT_TO_SPEECH.md).

### 2885.1
- CoverAlone single-page spread: `applySpreadLayout` accepts N≥1 (prune +
  place). Docs: [docs/SPREAD.md](docs/SPREAD.md).

### 2884.2
- Fix: `SessionDocument` private members for gallery layout mode/columns.

### 2884.1
- Gallery layout mode and columns in session + project JSON.

### 2883.1 / 2882.1 / 2881.*
- TTS document speak; spread equal height; pipewire; Gallery↔Image doc.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
