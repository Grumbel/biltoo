# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2459.1-fix-mapfromscene-overloads** (base `7d823d8`).

### Ownership refactor status
- Dual ImageView Stage 0–2c.3 LANDED
- **Fix 2459:** `using QGraphicsView::mapFromScene` (same hiding issue as mapToScene)

### Verification
| Check | Result |
|-------|--------|
| mapFromScene(QRectF) call sites | compile via using |
| pageguide / group chrome | build fix |
| Runtime / nix build | not run in sandbox |

### Next
- Optional lock-step dual nav
- QTimer lifetime policy for host switch
- Confirm full rebuild on host

### Apply
```bash
git pull --ff-only …/biltoo-2459.1-fix-mapfromscene-overloads-7d823d8.bundle HEAD
```
