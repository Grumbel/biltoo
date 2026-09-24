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
