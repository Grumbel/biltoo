# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2609.1-restore-framing-host-overrides** (base `7d823d8`).

### This tip
Restored five **DisplayPipelineHost** thin overrides deleted by the transform
peel (bodies lived on ImageController; host surface must stay on ImageView):

- `fitItem`
- `captureStickyPanAnchor`
- `applyImageModeFraming`
- `preserveImageViewOnLogicalSizeChange`
- `syncImageModeSceneRect`

Audit: all 61 host pure virtuals now have ImageView cpp or inline bodies.

### Rule going forward
**Never delete an ImageView method that is a DisplayPipelineHost override**
unless the virtual is removed from the host interface first. Pure-forward
peels apply only to *non-host* public convenience API.

### Wave C
Mode shell dispatchers (`setViewMode` / `setLayoutMode` / `reload*`) stay —
already thin.

### Still open
- statusText / hudFileName / loadingStatusHudLine gather
- Shell query surface (itemPaths, selectedPaths, …)
- Public-API size: header ~524 lines

### Apply
```bash
git pull --ff-only …/biltoo-2609.1-restore-framing-host-overrides-7d823d8.bundle HEAD
```
