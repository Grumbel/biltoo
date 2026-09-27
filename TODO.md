# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2723.1-hard-reload-text-layers` (base `9795ce1`).

### 2723.1 — Shift+F5 clears text layers
- Requires **thumtoo 347.1** (`purge_uri` / `purge_path` delete `page_text_layer`).
- Image hard reload calls `hostText().refresh()` after durable purge.

### Apply
```bash
git pull --ff-only …/biltoo-2723.1-hard-reload-text-layers-9795ce1.bundle HEAD
```
Needs thumtoo **347.1**.
