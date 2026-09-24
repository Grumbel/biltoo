# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2601.1-façade-only-root** (base `7d823d8`).

### ImageView sorted into domain subdirectories
Root `src/` now holds only the QGraphicsView façade:

- `imageview.h` / `imageview.cpp` — class, members, ctor/dtor, host includes
- `imageitem*`, `imageview_types.h`, `main.cpp`

All former `imageview_*.cpp` method TUs live as `*/imageview_routers.cpp`:

| Domain | Contents |
|--------|----------|
| `image/` | transform, appearance commit, color grade, framing/zoom |
| `crop/` | crop appearance routers |
| `text/` | text layer routers |
| `workspace/` | canvas, session bind/remove, pageguide, selection |
| `view/` | paint, input, **setViewMode mode shell** |
| `display/` | export, size book / contentLayoutSize |
| `session/` | appearance + item_state host gather |
| `hud/` | status / HUD routers |
| `shell/` | public accessors (MainWindow API) |

### Prior
**2600.1** Co-locate first batch of thin routers  
**2599.1** ImageController live meta / commit  

### Apply
```bash
git pull --ff-only …/biltoo-2601.1-façade-only-root-7d823d8.bundle HEAD
```
