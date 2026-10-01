# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2888.1-tts-speaking-highlight (linear stack on `e345338`).

### 2888.1
- TTS speaking highlight: stop clearing when `classicPath != m_ttsSpeakPath`.
  Paint already filters by session id. Init `m_ttsSpeakPath` from the anchor
  page, not `spans.first()`. Track active speaking page; only
  `setCurrentIndex` when `speech/followPages` is on.

### 2887.1
- Startup with no CLI files opens last session (`openLastSession` public).

### 2886.1
- TTS page follow optional; selection seek only on matching anchor.

### 2885.1
- CoverAlone single-page spread layout.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
Each tip bundle must `git pull --ff-only` onto current `origin/master`.
