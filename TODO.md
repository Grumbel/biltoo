# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2852.1-qset-cbegin (on `ea477d6` + agent stack).

### 2852.1
- Soft F5 flash label: `QSet` has no `constFirst()` — use `*paths.cbegin()`
  when `size() == 1`. (Hard reload still uses `QStringList::constFirst()`.)

### Prior
- 2851.1 shortcuts panel fill
- 2850.1 F5 QSet warning attempt
- 2849.1 contact/strip pack

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
