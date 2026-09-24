# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2480.2-fix-text-recompute-decl** (base `7d823d8`).

### Fix
- Drop ImageView::recomputeTextSearchMatches thin router (declaration was
  removed with TextLayerController ownership; method lives only on controller)

### Apply
```bash
git pull --ff-only …/biltoo-2480.2-fix-text-recompute-decl-7d823d8.bundle HEAD
```
