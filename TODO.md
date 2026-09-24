# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2464.1-own-workspace-group-transform** (base `7d823d8`).

### Ownership transfer (real)
- **Group scale/rotate** session + behaviour moved from ImageView façade to
  `WorkspaceController` (`workspace_group.cpp`).
- `GroupTransformSession m_groupXform` lives on the controller.
- ImageView keeps input routing only (`tryMouseMove/ReleaseGroup*`).

### Dual (prior stack)
- Dual compare works (per-surface pipeline, software secondary viewport).

### Next ownership cuts
- Page guide (ImageView → WorkspaceController) same pattern
- Optional: drop residual try* hop when input router is extracted

### Apply
```bash
git pull --ff-only …/biltoo-2464.1-own-workspace-group-transform-7d823d8.bundle HEAD
```
