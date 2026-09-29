# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2814.1-load-error-ui` (base `2085c07`).

### 2814.1
- formatLoadErrorMessage: file-not-found + MuPDF last error
- Status bar + Messages dock on load failure
- Expand reports open failures for PDF/md/text

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-354.6-mupdf-error-ui-fb6a408.bundle HEAD
git pull --ff-only …/biltoo-2814.1-load-error-ui-2085c07.bundle HEAD
```
