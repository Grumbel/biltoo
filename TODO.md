# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2457.1-stage-2c2-dual-image-shell** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 pixel/layout/bake: CLOSED**
- **Dual ImageView Stage 0–2b: LANDED**
- **Stage 2c.0–2c.1: LANDED** — active host, shared ItemWorld, shared pipeline ptr
- **Stage 2c.2: LANDED** — DualImageShell + View → Dual compare

### Stage 2c.2 summary
- `DualImageShell`: splitter, secondary host, focus → `setActiveHost`
- MainWindow central = shell; primary is still `m_imageView`
- Action: View → Dual compare (`Ctrl+Shift+D`), Image mode only
- Secondary: shared ItemWorld + pipeline; empty until explicit load (next)

### Verification (static)
| Check | Result |
|-------|--------|
| Secondary dtor does not tear down pipeline | Stage 2c.1 dtor guard |
| setActiveHost on focus | DualImageShell::noteFocus |
| Gallery/session chrome | still primary-only |
| Runtime / nix build | not run in sandbox |

### Next
- Stage 2c.3: open a second SessionImageId on the secondary pane; optional lock-step nav
- QTimer lifetime policy for host switch
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2457.1-stage-2c2-dual-image-shell-7d823d8.bundle HEAD
```
