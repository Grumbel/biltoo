# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2623.1-merge-private-incs** (base `7d823d8`).

### This tip
Merged `imageview_private_methods.inc` + `imageview_private_rest.inc` →
single `imageview_private.inc`. Updated `imageview.h`, surface doc, REFACTOR note.

Host `imageview_host_*.inc` files stay split (intentional).

### Apply
```bash
git pull --ff-only …/biltoo-2623.1-merge-private-incs-7d823d8.bundle HEAD
```
