# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2344-release-0.2.0-doc.**

Release planning document: [docs/RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md).

Known 0.2 limitation: EPUB/PDF Find matches **per text region** only —
phrases split across text boxes do not match (post-0.2 work).

Requires **thumtoo-323**. Code tip includes 2318–2343 (reorder, export, open
selection, Gallery drag pixmap).

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2344.1-release-0.2.0-doc-2f201f6.bundle HEAD
```

Next: **2345** (release cut) or post-0.2 text-search cross-region.

## Backlog
- EPUB/PDF text search across text-box boundaries (see RELEASE_0.2.0 §4.1)
- Release cut: VERSION 0.2.0, tag, flake pin, smoke matrix
