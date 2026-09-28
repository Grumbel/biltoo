# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2737.3-fix-textselection-metatype` (base `636e70e`).

### Spread P0–P4 done; build fix

- **2737.3:** `Q_DECLARE_METATYPE(TextSelection)` was *after* `#endif`, so each
  include redefined `QMetaTypeId<TextSelection>` and broke the build. Removed;
  panel signals use direct connections.

### Apply
```bash
git pull --ff-only …/biltoo-2737.3-fix-textselection-metatype-636e70e.bundle HEAD
```
