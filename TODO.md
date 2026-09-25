# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2647.1-batch-colour-apply` (base `3e9067c`).

### Done
- Orient multi-apply + undo macro (2646).
- **Colour multi-apply:** Adjustments panel **Apply to selection** →
  `applyColorAdjustmentsToTargets` with undo macro. Sliders still edit current
  target only. Button enabled when `transformTargets().size() > 1`.

### Next
1. Crop panel (manual / autocrop+threshold / margins / reset).
2. Expand targets beyond live items; template/stack later.

### Apply
```bash
git pull --ff-only …/biltoo-2647.1-batch-colour-apply-3e9067c.bundle HEAD
```
