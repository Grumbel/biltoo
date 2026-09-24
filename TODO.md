# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2465.1-own-workspace-page-guide** (base `7d823d8`).

### Ownership transfer
- **2464:** Workspace multi-select group transform → `WorkspaceController`
- **2465:** Print page-guide session + behaviour → `WorkspaceController`
  (`workspace_pageguide.cpp`). ImageView keeps thin public API +
  `renderForPrint` + input try*.

### Next
- Further façade cuts (paint/input collaborators) as needed
- Dual PreferCache coordination (optional)

### Apply
```bash
git pull --ff-only …/biltoo-2465.1-own-workspace-page-guide-7d823d8.bundle HEAD
```
