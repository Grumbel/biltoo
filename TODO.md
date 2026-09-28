# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2737.1-spread-p4-complete` (base `636e70e`).

### Spread multi-page reading surface — **DONE**

| Phase | Bundle | Summary |
|-------|--------|---------|
| P0–P1 | …–2731.x | Membership, layout, multi-underlay install, nav, status, leave rules |
| P2 | 2732–2733 | Cross-page text select/copy, panel flatten, multi search |
| P3 | 2734–2735 | TTS plan with sid, secondary glyphs, Gallery multi-member rings |
| P4 | 2736–2737 | Binding UI, LTR/RTL/Vertical, fixed-N 2–4, QSettings persistence |

### 2737.1
- `SpreadDirection::Vertical` + stacked layoutSpread
- View menu Vertical direction
- QSettings: `spread/binding`, `spread/direction`, `spread/fixedN`
- Unit tests: RTL + vertical

### Verify (static)
- Multi-underlay install (`imageModeItemForPath` / `isImageModeInstallPath`) present
- Text member layers, panel SessionIdRole, SpeakSpan.sessionId, Gallery paths API present
- Menu + settings + layout kernel present
- No open P0–P4 work items

### Apply
```bash
git pull --ff-only …/biltoo-2737.1-spread-p4-complete-636e70e.bundle HEAD
```
