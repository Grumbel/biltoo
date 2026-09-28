# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2767.2-filmstrip-no-titlebar` (base `b8a0cf3`).

### Stack (linear from origin)
1. **2766.1** — hide title bars when tabs visible (no AlwaysShowTabs)
2. **2767.2** — filmstrip zero-height title bar; Gallery open()/close()

### 2767.2
- Filmstrip: DockViewFactory zero-height title bar
- Gallery filmstrip: open()/close() instead of setVisible

### Apply (from origin/master `b8a0cf3`)
```bash
git pull --ff-only …/biltoo-2766.1-dock-hide-titlebar-when-tabs-b8a0cf3.bundle HEAD
git pull --ff-only …/biltoo-2767.2-filmstrip-no-titlebar-b8a0cf3.bundle HEAD
```
Or only the tip bundle (full stack from base):
```bash
git pull --ff-only …/biltoo-2767.2-filmstrip-no-titlebar-b8a0cf3.bundle HEAD
```
