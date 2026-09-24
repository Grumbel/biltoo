# TODO / agent handoff

## Status (2026-09-25)

**Tip: biltoo-2640.1-gallery-bar-range-recenter** (base `64c7465`).

## Research answer (signals / SO)

There is **no** public “scrollbar appeared” signal on QAbstractScrollArea.

Qt shows/hides AsNeeded bars from:
`QScrollBar::rangeChanged` → internal `_q_showOrHideScrollBars` (QueuedConnection)
(see qabstractscrollarea.cpp).

Community patterns:
- Connect to `rangeChanged` (this tip)
- Event-filter Show/Hide on the bar widgets (Qt forum)
- AlwaysOn (avoids the problem; not desired here)
- SO 38254367: same “contents shift when scrollbar appears” — no solid accepted fix

This tip: on rangeChanged in Gallery, capture scene centre under the viewport,
then `QTimer::singleShot(0)` re-centre after Qt’s queued bar layout.

### Apply
```bash
git pull --ff-only …/biltoo-2640.1-gallery-bar-range-recenter-64c7465.bundle HEAD
```
