# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2857.1-contact-sheet-cols (on `ea477d6` + agent stack).

### 2857.1
- **Contact Sheet**: pack exactly `Columns` pages per full row and scale the
  row to fill width (no almost-fit trailing gutter). Last partial row stays
  nominal column scale unless already ~full.
- **Strip Rows**: if the next page almost fits, squeeze the row up to 12%
  instead of wrapping early.

### Prior
- 2856.1 toolbar toggleViewAction
- 2855.1 zoom/text icons

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
