# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2651.1-filmstrip-materialize-gui` (base `9740316`).

### Done
- Crop panel / autocrop wait samples (2650).
- **2651.1:** Session remove → filmstrip `setCurrentIndex` could
  `ASSERT_NOT_GUI_THREAD` in `materializeDisplay` when ItemWorld crop + host
  cache sample > `kGuiMaterializeMaxEdge`. `applyStoredAppearanceToThumb`
  clamps to GUI max before SoftPreview bake.

### Next
1. Crop panel polish: live preview; normalised fields.
2. Template / even-odd / stack (later).

### Apply
```bash
git pull --ff-only …/biltoo-2651.1-filmstrip-materialize-gui-9740316.bundle HEAD
```
