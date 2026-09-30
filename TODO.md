# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2859.1-flow-layout-fix (on `ea477d6` + agent stack).

### 2859.1
- **Flow** (was Contact Sheet): full rows fill width; **last row always left-aligned**
  (no enlarge leftovers).
- **Flow Rows** (was Strip Rows): restore uniform band-height wrap; last row
  dangling; no progressive scale-to-the-right; optional flush only on non-final rows.
- UI/help renamed to Flow / Flow Rows (enum values ContactSheet/StripRows kept).

### Prior
- 2858.1 F5 QStringList
- 2857.1 contact sheet columns

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
