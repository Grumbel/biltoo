# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2803.1-bookshelf-menu` (base `a989daf`).

### 2803.1
- **Bookshelf** menu (Phase 1): pin current session path list, open/remove entries,
  clear all; QSettings `bookshelf` array (no auto-eviction)
- **txt/md:** wait for MuPDF ≥ 1.28 (Markdown in 1.28.0-rc1); Nix still on 1.27.2
  — noted in `docs/TXT_MD_SUPPORT.md`

### Apply
```bash
git pull --ff-only …/biltoo-2803.1-bookshelf-menu-a989daf.bundle HEAD
```

### Still open
- Bookshelf Phase 2/3 (search UI, cover grid) — docs/BOOKSHELF.md
- txt/md after MuPDF 1.28 lands in the flake
