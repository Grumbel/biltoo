# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2627.2-host-fwd-and-test-fititem** (base `fe7adfe`).

### This tip
1. Forward-declare `TextLayerController` in `displaypipelinehost.h` (hostText pure virtuals)
2. Characterization tests: `view.fitItem` → `view.hostImage().fitItem` after
   ImageView peel removed the thin forward (15f3114)

### Host virtuals remaining: ~57

### Apply
```bash
# Supersedes 2627.1 — full stack from origin/master (fe7adfe)
git pull --ff-only …/biltoo-2627.2-host-fwd-and-test-fititem-fe7adfe.bundle HEAD
```
If you already applied 2627.1 with a different SHA for the forward-decl commit,
reset to `origin/master` first, then pull this bundle.
