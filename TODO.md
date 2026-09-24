# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2450.1-display-pipeline-host-stage1** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake ownership: CLOSED** (re-verified)
- **Phase 6 Tier 0 exit: MET**
- **Dual ImageView Stage 0: LANDED** (2449) — DisplayPipelineHost core
- **Dual ImageView Stage 1: LANDED** (2450) — controllers + appearance + framing

### Stage 1 summary
- Host: slideshow/crop/gallery/image/framing/gallerySizeResolve; appearance
  helpers; primary/target; framing/sticky; setUpdatesEnabled; status notify;
  hostObject
- Pipeline: ~471 `m_host->` / ~76 residual `m_view->` (tr, load/place, pack,
  text layer, mapFrom/ToScene, …)
- QPointer guards still `ImageView` (async ImageView-only APIs)

### Verification (static, 2026-09-24)
| Check | Result |
|-------|--------|
| ImageItem friends | sole DisplayPipelineController |
| Pixel mutators outside pipeline | none |
| bake/rematerialize TUs | absent |
| Host pure virtuals | 50; ImageView overrides present |
| Phase 5 rules | hold |

Runtime / `nix build`: **not run**.

### Next
- Stage 1b: residual m_view long-tail
- Stage 2: active-host switching; dual-pane shell
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2450.1-display-pipeline-host-stage1-7d823d8.bundle HEAD
```
