# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2858.1-f5-stringlist (on `ea477d6` + agent stack).

### 2858.1
- Soft F5 unique paths use `QStringList` (not `QSet`) so the single-path flash
  label does not trip GCC `-Wnull-dereference` on QSet iterators.

### Prior
- 2857.1 contact sheet columns
- 2856.1 toolbar toggleViewAction

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
