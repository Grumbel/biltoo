# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2453.1-pipeline-host-only-no-view** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–2b: LANDED** — pipeline bound only to DisplayPipelineHost

### Stage 2b summary
- Dropped `m_view` / `view()` from DisplayPipelineController
- Ctor: `DisplayPipelineController(DisplayPipelineHost *host)`
- ImageView constructs with `this` (is the host)

### Verification (static)
| Check | Result |
|-------|--------|
| ImageItem friends | sole DisplayPipelineController |
| Pixel mutators outside pipeline | none |
| Pipeline `m_view` / `view()` | **removed** |
| `QPointer<ImageView>` in display/ | **0** |
| Host pure virtuals | ~105 |
| Runtime / nix build | not run |

### Next
- Stage 2c: dual-pane shell + active host / shared ItemWorld
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2453.1-pipeline-host-only-no-view-7d823d8.bundle HEAD
```
