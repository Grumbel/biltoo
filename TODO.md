# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2449.1-display-pipeline-host-stage0** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake ownership: CLOSED** (re-verified 2448.1)
- **Phase 6 Tier 0 exit: MET** (`imageview.h` ~693 lines)
- **Dual ImageView Stage 0: LANDED** — `DisplayPipelineHost` + pipeline `m_host`

### Dual ImageView Stage 0
- New `src/display/displaypipelinehost.h` (dual-critical virtual surface)
- `ImageView` implements `DisplayPipelineHost` (public)
- `DisplayPipelineController` stores `m_host` + `m_view`; Stage 0 sites use host
  for ItemWorld / liveItems / canvasScene / mode / session books / path raster /
  logicalSize / viewportWidget
- Long-tail still on `view()` — Stage 1 migrates remainder onto host
- Topology + stages documented in `docs/IMAGEVIEW_ITEM_OWNERSHIP.md`

### Verification (static)
Phase 5 checklist (2448.1) still holds. Stage 0:
- `DisplayPipelineHost` pure virtuals matched by ImageView overrides
- No new ImageItem friends
- Pixel mutators still only via pipeline

Runtime / full `nix build`: **not run** (sandbox).

### Next
- **Stage 1:** grow host surface; shrink `m_view` long-tail
- Optional: paint/input Host extraction (REFACTOR.md)
- Product dual-pane shell after Stage 1 host is wide enough

### Apply
```bash
git pull --ff-only …/biltoo-2449.1-display-pipeline-host-stage0-7d823d8.bundle HEAD
```
