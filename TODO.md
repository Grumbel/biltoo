# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2712.1-shared-view-tools` (base `b65f69e`).

### 2712.1 — Shared Select/Pan/Zoom tool (Phase A)
- `ViewInteraction` owns `Tool` on ImageView (not Workspace-only).
- Defaults on mode enter: Image → Pan, Gallery/Workspace → Select.
- Left tools strip visible in all modes; V/H/Z shortcuts.
- Zoom-region tool works outside Workspace when Zoom is active.
- Image-mode left-drag behaviour **unchanged** (still chrome preference).

### 2711.x — Dock layout + Panels top-level menu

### Next (tools)
- Phase B/C: Image Select → text rubber-band; Space-to-pan override
- Crop on toolbox (modal enter)
- Multi-page TextSelection data

### Apply
```bash
git pull --rebase …/biltoo-2712.1-shared-view-tools-b65f69e.bundle HEAD
```
