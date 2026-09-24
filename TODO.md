# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2448.1-verify-phase5-ownership** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake ownership: CLOSED** (re-verified this tip)
- **Phase 6 Tier 0 exit: MET** (`imageview.h` 693 lines; public surface within exit)
- Fix 2446: `restoreStickyPanAnchor` public (characterization + framing pair)
- No privatized methods referenced from `tests/` (scan clean)

### Verification (static, 2026-09-24 agent pass)
| Check | Result |
|-------|--------|
| ImageItem friends | sole `DisplayPipelineController` |
| Pixel mutators (`setSourceImageReady` / `setPreviewImage` / `clearDecodedPixels` / tile bag attach) | only in `displaypipelinecontroller*.cpp` + `imageitem*.cpp` defs |
| External `setIntrinsicSize` on ImageItem | none (sole writer `hostSetIntrinsicSize`) |
| `imageview_bake.cpp` / `imageview_rematerialize.cpp` | absent |
| Characterization `restoreStickyPanAnchor` | public; used in `tests/imageview_characterization.cpp` |
| Privatized names in `tests/` | zero call sites (`bakeItemRotate90` only in a comment) |
| Mode controllers | Gallery/Workspace/Image use `hostDisplayPipeline()` for pixel/layout |
| ImageView friends | intentional: `ImageViewTransformGeometryCommand` only |

Runtime / full `nix build` / characterization binary: **not run** (sandbox; no Qt ≥6.9 / vips).

### Next (product)
- **Dual ImageView (0.3)** — shared pipeline + ItemWorld (see `docs/IMAGEVIEW_ITEM_OWNERSHIP.md` § Dual ImageView prerequisites)
- Optional: further Host extraction (paint/overlay, input router) per REFACTOR.md

### Dual ImageView — first design notes (not started)
Prerequisites already satisfied (Phase 5). Still required:
1. Two live Image-mode canvases without double-owning `m_items` / mode stashes
2. **Shared** `DisplayPipelineController` + `ItemWorld` + session id book (today both are ImageView members; pipeline holds `ImageView *`)
3. Focus / selection targeting left vs right by `SessionImageId`
4. Per-surface framing (`ViewFraming` / sticky pan); content bake remains session-global
5. Do not reintroduce ImageView pixel mutator friendship

Suggested first code step (when starting 0.3): introduce a narrow host/session owner so pipeline + ItemWorld are not tied to a single `ImageView *`, then thin dual-pane shell. Discuss before large moves.

### Apply
```bash
git pull --ff-only …/biltoo-2448.1-verify-phase5-ownership-7d823d8.bundle HEAD
```
