# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2579.1-hud-appearance-centre-progress** (base `7d823d8`).

### Ownership transfer
- **HudChrome** appearance mutators: setVisible / setFontPointSize / setTextColor / setPanelColor
  (afterChange callback; ImageView routers thin)
- **ViewShellChrome::setCentreProgress / clearCentreProgress** — bag + FullViewportUpdate policy
- ImageView `setCentreProgress` / `clearCentreProgress` are thin shell routers
- Slimmed `imageview_input.cpp` includes (edge/wheel/resize only)

### Prior
**2578.1** GalleryController owns pixel-mix / loading tile counts  
**2577.1** HudModel pure status helpers; ViewShellChrome::restoreToolCursor  
**2576.1** Own canvas material mutators on ViewShellChrome

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers beyond materials (keep until dual/callers migrate)
- slideshow coupling on setHudVisible (SlideshowController host)

### Apply
```bash
git pull --ff-only …/biltoo-2579.1-hud-appearance-centre-progress-7d823d8.bundle HEAD
```
