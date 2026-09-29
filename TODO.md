# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.4-stack-linear (base 2085c07).

### 2815.4
- Linear tip on origin stack; supersedes divergent sibling bundle e1ae398

### 2815.3
- formatLoadErrorMessage: never QFileInfo::exists — detail only from mupdf_last_error()
- Dropped allowFilesystemStat; empty Open / expand reports no longer stat

### Apply
```bash
git pull --ff-only …/biltoo-2815.4-stack-linear-2085c07.bundle HEAD
```

Fast-forward from any ancestor of this tip (including 1149901 / 1cb81855).
