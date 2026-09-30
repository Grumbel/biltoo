# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2844.1 (on origin `6ad534a`).

### Stack on origin/master 6ad534a
1. LOD planner negative-scale content stride
2. SessionListStore (Recent/Bookshelf → XDG state, debounced)

### Bundle rule
Base = current origin/master HEAD at tip creation. Never re-root on an older
origin commit when origin has advanced. Fix-forward only.
