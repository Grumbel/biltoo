# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2444.1-phase5-complete-tier0-exit** (base `d80d461`).

### Phase 5 ownership — COMPLETE
- Sole ImageItem pixel friend: DisplayPipelineController
- No ImageView pixel/layout thin-forward TUs (bake + rematerialize deleted)
- Mode controllers + ImageView TUs call pipeline directly
- Content bake rotate/flip on pipeline
- Intrinsic sole writer: hostSetIntrinsicSize

### Phase 6 Tier 0 — exit criteria met
- `imageview.h` ~693 lines (target &lt;1000)
- ~217 public methods (target &lt;260)
- Event overrides already `protected`
- Further privatize is optional polish

### Residual (not blocking Phase 5)
- Interactive grade: live-grade fast path + filmstrip `sessionAppearanceChanged` emit
- Bake host helpers public for pipeline (capture/undo/persist)
- `rotateContentByQuarterTurns` public (framing after pipeline bake)

### Next
- Dual ImageView (0.3) — product track; shares pipeline + ItemWorld
- Optional: more Tier 0 privatize of non-host public methods

### Apply
```bash
git pull --ff-only …/biltoo-2444.1-phase5-complete-tier0-exit-d80d461.bundle HEAD
```
