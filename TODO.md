# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2813.3-text-ext-and-force` (base `2085c07`).

### 2813.3
- Broad text suffixes; `//text` force ref + expandTextForceToPageRefs
- Needs thumtoo **354.3**

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-354.3-text-ext-and-force-fb6a408.bundle HEAD
git pull --ff-only …/biltoo-2813.3-text-ext-and-force-2085c07.bundle HEAD
```
