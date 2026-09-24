# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2624.1-imageview-peel-plateau-docs** (base `7d823d8`).

### This tip
Document **ImageView peel plateau** in AGENTS.md + IMAGEVIEW_SURFACE.md:

- Pure-forward public peels are complete
- Remaining thin `ImageView::` methods are host overrides or private routers
- Do not delete host-surface forwards without changing DisplayPipelineHost

### Series summary (2603–2624)
SelectionGeometry fix → pure peels → host-override restores → link/test fallout
→ status HudModel assemblers + tests → private-inc merge → plateau docs.

### Apply
```bash
git pull --ff-only …/biltoo-2624.1-imageview-peel-plateau-docs-7d823d8.bundle HEAD
```
