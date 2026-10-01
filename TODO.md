# TODO / agent handoff

## Status (2026-10-01)

**Tip:** biltoo-2887.1-open-last-session-on-startup (linear stack on `e345338`).

### 2887.1
- Startup with no CLI files/dirs opens the last session (File → History head)
  via **public** `MainWindow::openLastSession()` (next to `loadFiles`). Empty
  history → empty window. README notes the behaviour.

### 2886.1
- TTS: page follow optional (`speech/followPages`, default off); selection seek
  only when `speakAnchorOffset` matches (no jump to document start).

### 2885.1
- CoverAlone single-page spread: `applySpreadLayout` accepts N≥1.

### Bundle policy
Work-line base: `e345338`. Full stack `e345338..HEAD`. No parallel histories.
Each tip bundle must `git pull --ff-only` onto `origin/master` at build time.
