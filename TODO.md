# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2337-export-page-ref-overwrite.**

Fix: Export Images to folder refused PDF `//page:N` (and other virtual paths)
because `QFileInfo::canonicalFilePath()` on refs is empty or collapses `//`,
so empty==empty looked like overwriting the source. Virtual paths never
collide; real-file compare requires non-empty canonical. Stems for page /
archive / pdf-image refs use parsed names (`book_p2`, member basename).

Requires **thumtoo-323**. Includes 2318–2336.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2337.1-export-page-ref-overwrite-2f201f6.bundle HEAD
```

Next: **2338**.

## Backlog
- Gallery-canvas drag reorder (drag tiles on the packed canvas itself)
