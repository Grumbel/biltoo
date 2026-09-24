# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2456.1-stage-2c1-shared-pipeline-ptr** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–2b: LANDED**
- **Stage 2c.0: LANDED** — `setActiveHost` + `bindSharedItemWorld`
- **Stage 2c.1: LANDED** — pipeline `unique_ptr` + `bindSharedDisplayPipeline`

### Stage 2c.1 summary
- `m_ownedPipeline` + `m_displayPipeline *` (all TUs use pointer)
- `bindSharedDisplayPipeline` / `hasSharedDisplayPipeline`
- Dual shell wire order documented (IMAGEVIEW_ITEM_OWNERSHIP.md)

### Verification (static)
| Check | Result |
|-------|--------|
| `m_displayPipeline.` residual | none (all `->`) |
| Single-pane ctor | owns unique_ptr pipeline |
| Dual-pane shell in MainWindow | **not started** |
| Runtime / nix build | not run in sandbox |

### Next
- Stage 2c.2: dual-pane shell in MainWindow (splitter, focus → setActiveHost)
- QTimer lifetime policy for host switch
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2456.1-stage-2c1-shared-pipeline-ptr-7d823d8.bundle HEAD
```
