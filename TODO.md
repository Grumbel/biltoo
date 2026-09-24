# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2616.1-drop-dead-private-decls** (base `7d823d8`).

### This tip
Remove private ImageView declarations with no definitions (bodies lived on
controllers after the transform peel; nothing called the ImageView names):

- `destroyDoomedWorkspaceItems`
- `finishSetWorkspacePaths`
- `zoomViewBy`

### Peel series summary (2603–2616)
1. Pure-forward public API → hostImage/Workspace/Gallery/Shell/Hud/Crop/Text
2. Host-override restores (reorder, framing, edgeZone, sticky pan, cancelZoom)
3. Link/test fallout (flashHud, raise/lower, appearance stub, characterization)
4. Dead private decl cleanup

**ImageView public surface now:** mode shell dispatchers, status gather, shell
queries, DisplayPipelineHost + QGraphicsView, remaining host-surface thins.

### Next (optional)
- statusText / hudFileName gather → pure HudModel inputs
- Further public query peels (itemPaths, selectedPaths) if desired

### Apply
```bash
git pull --ff-only …/biltoo-2616.1-drop-dead-private-decls-7d823d8.bundle HEAD
```
