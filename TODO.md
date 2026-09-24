# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2610.1-peel-session-appearance-itemworld** (base `7d823d8`).

### This tip
- Peeled `hasSessionAppearance` / `setSessionAppearance` →
  `itemWorld().hasDurableAppearance` / `itemWorld().setAppearance`.
- **Bug fix:** those methods (and `sessionAppearanceValue`) used `m_itemWorld`
  directly; dual-view secondary hosts must use `itemWorld()` (shared world).

`sessionAppearanceValue` stays as DisplayPipelineHost override (now via
`itemWorld().appearanceValue`).

### Series (2603–2610)
Pure-forward peels + host-override restores + shared ItemWorld routing fix.

### Still on ImageView
- Mode shell: setViewMode / setLayoutMode / reload*
- Status gather: statusText / hudFileName / loadingStatusHudLine
- Shell queries: itemPaths / selectedPaths / imageSize / pendingDecodeCount
- Host surface + QGraphicsView

### Apply
```bash
git pull --ff-only …/biltoo-2610.1-peel-session-appearance-itemworld-7d823d8.bundle HEAD
```
