# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2712.3-tools-strip-crop` (base `b65f69e`).

### 2712.3 — Tools strip + Crop
- Left strip renamed **Tools**; includes Select / Pan / Zoom / **Crop**.
- Switching Select/Pan/Zoom while crop is active **commits** the crop draft.
- Crop still on main toolbar + Image menu (C).

### 2712.2 — Tool-aware pan + Select→text rubber
### 2712.1 — Shared ViewInteraction tool ownership

### Next
- Multi-page TextSelection data
- Click-select single text region
- Attention on tools strip (optional)

### Apply
```bash
git pull --rebase …/biltoo-2712.3-tools-strip-crop-b65f69e.bundle HEAD
```
