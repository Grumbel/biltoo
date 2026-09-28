# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2739.1-doubleview-toolbar-zoom-persist` (base `636e70e`).

### 2739.1 Double View UX
- Toolbar split button (toggle + menu: binding / direction / N)
- Sticky Double View across Gallery → Image (FixedN kept; selection spreads still clear)
- Zoom preserved: `applySpreadLayout` only fitInView on membership change / forceFit
- Scene rect from item scene bounds + centerOn after fit

### Apply
```bash
git pull --ff-only …/biltoo-2739.1-doubleview-toolbar-zoom-persist-636e70e.bundle HEAD
```
