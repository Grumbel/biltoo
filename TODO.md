# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2625.1-host-narrow-text** (base `7d823d8`).

### This tip — DisplayPipelineHost narrowing (cluster: text)
- Added `hostText()` / `const hostText()` pure virtuals
- Removed `clearTextSelection` and `refreshTextLayer` virtuals
- Pipeline: `m_host->hostText().clearSelection()` / `.refresh()`
- Dropped ImageView thin routers for those two methods

### Host narrowing rule
Prefer **controller accessors on the host** over one-off action virtuals.
Delete the virtual first; update pipeline; then drop ImageView override.

### Next host clusters (suggested)
Framing (`fitItem`, sticky pan, scene rect) — higher use; needs careful stage.
Or more low-use action virtuals like text (one-call-site).

### Apply
```bash
git pull --ff-only …/biltoo-2625.1-host-narrow-text-7d823d8.bundle HEAD
```
