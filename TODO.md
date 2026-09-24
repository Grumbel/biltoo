# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2466.1-own-workspace-chrome** (base `7d823d8`).

### Ownership transfer
- **2464:** group transform → WorkspaceController
- **2465:** page guide → WorkspaceController
- **2466:** Workspace chrome input + `ItemInteractSession` → WorkspaceController
  (`workspace_chrome.cpp`). ImageView input router calls `m_workspace.try*`.

### Next
- paint collaborator / size-book (optional)
- Dual PreferCache coordination (optional)

### Apply
```bash
git pull --ff-only …/biltoo-2466.1-own-workspace-chrome-7d823d8.bundle HEAD
```
