# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2845.1-epub-f5-process-cache (on `ea477d6` + agent commits).

### 2845.1
- Soft F5 (Image / Gallery / Workspace): always clear process tile/ladder
  caches and re-decode, even when the source fingerprint is unchanged.
  Durable Store still requires Shift+F5 (needs thumtoo-011.1 region purge).
- EPUB layout collision fixed in **thumtoo-011.1** (layout in region key).

### Bundle policy
Every tip bundle = full stack from the work-line base (`ea477d6`) to HEAD.
