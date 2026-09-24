# ImageView surface (agent handoff)

## Intent

ImageView is the **QGraphicsView + DisplayPipelineHost** facade and **mode shell**.
Domain logic lives on controllers (`ImageController`, `WorkspaceController`,
`GalleryController`, `HudChrome`, …). Thin routers stay in domain
`*/imageview_routers.cpp` files — do **not** reunite into one TU.

## Keep on ImageView

| Kind | Examples |
|------|----------|
| Mode shell | `setViewMode`, `setLayoutMode`, `reloadFromDisk`, `hardReloadFromDisk` |
| Status entry points | `statusText`, `hudFileName`, `loadingStatusHudLine`, `refreshStatus` |
| Shell queries | `selectedPaths`, `pendingDecodeCount` |
| Host surface | Everything in `imageview_host_*.inc` / `DisplayPipelineHost` |
| QGraphicsView | paint, input overrides, viewport mapping |

Status **formatting** is pure `HudModel` (`formatMultiItemStatusLine`,
`formatImageModeStatusLine`, …). ImageView only **gathers** inputs.

## Peel rule

- Pure forwards → call `hostImage()` / `hostWorkspace()` / … on the controller.
- **Never** remove an ImageView method that is still a `DisplayPipelineHost`
  pure virtual or declared in `imageview_host_*.inc` without updating the host
  interface and all callers.
- Prefer not to push mode branching into MainWindow.

## Header layout

- `imageview.h` — public shell + includes
- `imageview_host_*.inc` — controller/pipeline host surface (keep split)
- `imageview_private.inc` — private helpers/members

## See also

- `docs/MODE_OWNERSHIP.md`, `docs/IMAGEVIEW_ITEM_OWNERSHIP.md` (if present)
- `TODO.md` for current tip / open work


## Peel plateau (2026-09)

Pure-forward public API peels are **done**. Remaining `ImageView::*` single-line
forwards are almost all:

1. **DisplayPipelineHost** overrides (must stay until the host interface shrinks), or
2. **Private** routers used only inside the ImageView translation units.

Do **not** delete host-surface methods “because they only call `m_image`.”
That caused link failures (framing, reorder, sticky pan, edge zones).

### Productive next work (not pure peels)

| Work | Notes |
|------|--------|
| Host interface reduction | Stage plan with DisplayPipelineController; remove virtuals first |
| Status tests | `hudmodel_test` covers pure formatters |
| capture/freeze helpers | Optional shared freeze policy — many controller callers |
| Characterization | Prefer `hostX()` / `itemWorld()` in new tests |

