# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2652.1-crop-polish-undo-macro` (base `9740316`).

### Done
- **2652.1**
  - Shared `util/contentundomacro.h` (colour / orient / crop batch).
  - Crop panel polish: normalised 0–1 margin fields (synced with px when page
    size known), status line (size → crop rect / need sample / full-frame),
    debounced soft **preview on current page only** (no durable write until Apply).

### Next
1. Targets beyond live items (session range / materialise virtual slots).
2. Template / even-odd / stack (later).

### Apply
```bash
git pull --ff-only …/biltoo-2652.1-crop-polish-undo-macro-9740316.bundle HEAD
```
