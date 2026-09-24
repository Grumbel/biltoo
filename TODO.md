# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2458.1-stage-2c3-secondary-session-load** (base `7d823d8`).

### Ownership refactor status
- **Phase 5 CLOSED**; Dual ImageView Stage 0–2c.3 **LANDED**

### Stage 2c.3 summary
- `openOnSecondary` / `navigateSecondary` on DualImageShell
- Dual enable seeds secondary with next session row
- goPrevious/goNext route to secondary when it has focus
- Secondary key/edge nav signals wired

### Verification (static)
| Check | Result |
|-------|--------|
| Shared pipeline install target | setActiveHost(secondary) before load |
| Primary m_currentIndex on secondary nav | unchanged |
| Secondary dtor vs pipeline | Stage 2c.1 guard |
| Runtime / nix build | not run in sandbox |

### Next
- Optional lock-step dual nav
- QTimer lifetime policy for host switch
- Optional: paint/input Host extraction

### Apply
```bash
git pull --ff-only …/biltoo-2458.1-stage-2c3-secondary-session-load-7d823d8.bundle HEAD
```
