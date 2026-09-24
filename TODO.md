# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2471.1-workspace-input-ownership** (base `7d823d8`).

### Real ownership transfer (Workspace input residual)
- Page-guide move/release → `WorkspaceController::tryMouseMove/ReleasePageGuide`
- Group scale/rotate + handle drag move/release → `WorkspaceController`
- Item move release → `WorkspaceController::tryMouseReleaseItemDrag`
- ImageView keeps thin routers only (closes residual after 2464–2466)

### Apply
```bash
git pull --ff-only …/biltoo-2471.1-workspace-input-ownership-7d823d8.bundle HEAD
```
