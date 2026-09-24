# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2436.1-imageview-tus-pipeline-direct** (base `d80d461`).

### Phase 5 ownership — continued
- Sole ImageItem pixel friend: DisplayPipelineController
- Controllers → hostDisplayPipeline (2435)
- **ImageView TUs → m_displayPipeline direct (2436)**
  - selection, canvas, appearance, crop, color_grade, size_book, framing
  - public ImageView methods remain thin forwards

### Verification
- Friend: only DisplayPipelineController
- Private pixel mutators: only displaypipelinecontroller.cpp
- ImageView TUs: pixel/layout via m_displayPipeline.*
- Full nix build not run in this sandbox

### Residual on ImageView
- interactive grade live-grade fast path + filmstrip emit
- bake host helpers (capture/undo)
- public thin forwards

### Next
- Phase 6 header closure / Dual ImageView (0.3)

### Apply
```bash
git pull --ff-only …/biltoo-2436.1-imageview-tus-pipeline-direct-d80d461.bundle HEAD
```
