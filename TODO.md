# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2349-src-session-subdir.**

Phase 1 of source layout: **`src/session/`** holds session document, appearance,
seed book, expand/export/open/sort/reorder, pack order views.

Includes use `session/foo.h` (tilelod style). See [docs/SRC_LAYOUT.md](docs/SRC_LAYOUT.md).

Requires **thumtoo-323**. Includes prior 0.2 feature + doc tips.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2349.1-src-session-subdir-2f201f6.bundle HEAD
```

Next: **2350** — `src/crop/` move (or build-verify session phase).

## Backlog (0.2.0)
- [x] `src/session/` (§3a phase 1)
- [ ] `src/crop/`
- [ ] `src/gallery/`
- [ ] `src/shell/`
- [ ] `src/display/` (optional pre-tag)
- [ ] RC smoke incl. crop + Open Selection
- [ ] VERSION 0.2.0 + tag
