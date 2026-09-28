# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2737.2-spread-verify-fixes` (base `636e70e`).

### Spread multi-page reading surface — **DONE + verified**

| Phase | Status |
|-------|--------|
| P0–P1 surface, install, nav, leave | done |
| P2 text / panel / search | done |
| P3 TTS + Gallery rings | done |
| P4 binding / LTR-RTL-Vertical / N / settings | done |

### 2737.2 verification fixes
- Apply `m_spreadDirection` when enabling FixedN and Selection spreads
- `Q_DECLARE_METATYPE(TextSelection)` + register for panel multi-select signal

### Static verification
- Integration points for install, text, TTS, Gallery, menu, settings: all present
- `paintSceneOverlays` brace balance OK
- Unit tests: FixedN, CoverAlone, RTL, vertical registered in CMake

### Apply
```bash
git pull --ff-only …/biltoo-2737.2-spread-verify-fixes-636e70e.bundle HEAD
```

No further spread work planned unless runtime QA finds issues.
