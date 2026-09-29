# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2814.3-open-fail-ui` (base `2085c07`).

### 2814.3
- Failed Open/Bookshelf: do not restore previous filmstrip
- Centre HUD "Could not open" + Messages + status bar
- emptyResultMessage uses formatLoadErrorMessage

### Note
Expand still opens PDFs for page count (no durable page_count read yet).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-354.7-mupdf-error-global-fb6a408.bundle HEAD
git pull --ff-only …/biltoo-2814.3-open-fail-ui-2085c07.bundle HEAD
```
