# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2600.1-domain-routers** (base `7d823d8`).

### Co-locate thin ImageView routers into domain subdirectories
Per SRC_LAYOUT: ownership transfer first, then routers live next to owners.

| Domain TU | Routers from (removed root files) |
|-----------|-----------------------------------|
| `image/imageview_routers.cpp` | transform_actions, appearance_commit, color_grade |
| `crop/imageview_routers.cpp` | crop_appearance |
| `text/imageview_routers.cpp` | text |
| `workspace/imageview_routers.cpp` | canvas*, session_bind/remove, pageguide, selection |
| `view/imageview_routers.cpp` | paint*, input* |
| `display/imageview_routers.cpp` | export |

### Root façade residual (host / shell — intentional)
- `imageview.cpp` / `.h` — QGraphicsView shell + member bags
- `imageview_modes.cpp` — setViewMode mode shell
- `imageview_appearance.cpp` / `_item_state.cpp` — appearance host gather
- `imageview_status.cpp` / `_accessors.cpp` / `_size_book.cpp` / `_framing_image.cpp`

### Prior
**2599.1** ImageController owns live meta, geometry persist, session commit  
**2598.1** Workspace owns pending bind take; test appearance stub

### Apply
```bash
git pull --ff-only …/biltoo-2600.1-domain-routers-7d823d8.bundle HEAD
```
