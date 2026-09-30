# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2850.1-f5-qset-warning (on `ea477d6` + agent stack).

### 2850.1
- Silence GCC `-Wnull-dereference` on Workspace/Gallery soft F5: drop redundant
  `QSet done` (paths already unique) and use `paths.constFirst()` instead of
  `*paths.constBegin()`.

### Prior
- 2849.1 contact/strip pack
- 2848.1 annot shape modifiers
- 2847.1 shortcuts panel

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
