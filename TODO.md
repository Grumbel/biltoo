# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2851.1-shortcuts-panel-fill (on `ea477d6` + agent stack).

### 2851.1
- Keyboard Shortcuts panel was empty after the dialog→panel change: fill on
  `showEvent` (layout restore / toggle), capture shortcut text at collect time,
  store `QAction*` as `QObject*` in the table, reentrancy guard.

### Prior
- 2850.1 F5 QSet warning
- 2849.1 contact/strip pack
- 2848.1 annot shape modifiers

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
