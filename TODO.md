# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2606.1-peel-text-hud-forwards** (base `7d823d8`).

### Wave B (this tip)
Added `hostHud()` (`HudChrome`). Peeled text + HUD public forwards.

| Was `ImageView::` | Now |
|-------------------|-----|
| setShowTextRegions / setTextSearch* / hasTextLayer / textLayerRegionCount / copySelectedText | hostText().… |
| textMatchesQuery | TextSearchPolicy::matches |
| setHudVisible / setHudFontPointSize / setHudTextColor / setHudPanelColor / flashHud | hostHud().… (+ viewport afterChange; setVisible also syncs slideshow timer) |

**Kept on ImageView (complex gather):** statusText, hudFileName, loadingStatusHudLine,
refreshStatus. Host overrides: clearTextSelection, refreshTextLayer.

Header ~599 → ~573 lines.

### Cumulative peels (2604–2606)
~60+ pure-forward public methods removed from ImageView.

### Next
- statusText / hudFileName / loadingStatusHudLine → HudModel or StatusPresenter
  (multi-controller gather; higher risk)
- Wave C: thin setViewMode dispatcher; push branch bodies into mode controllers
- Remaining public surface audit

### Apply
```bash
git pull --ff-only …/biltoo-2606.1-peel-text-hud-forwards-7d823d8.bundle HEAD
```
