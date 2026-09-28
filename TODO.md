# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2802.1-txt-bookshelf-design` (base `a989daf`).

### 2802.1 — design notes only
- `docs/TXT_MD_SUPPORT.md` — `.txt`/`.md` are **not** free via MuPDF like ePub;
  need thumtoo synthetic HTML/EPUB or biltoo QTextDocument path
- `docs/BOOKSHELF.md` — prefer **menu “Add to Bookshelf” + list** (Recent-like)
  before a graphical cover shelf

### Prior tip
- 2801.1 scroll HUD fixed
- 2800 KDDock center margins

### Apply
```bash
git pull --ff-only …/biltoo-2802.1-txt-bookshelf-design-a989daf.bundle HEAD
```

### Still open (product)
- Implement bookshelf Phase 1 when wanted
- txt/md only after thumtoo (or explicit biltoo-only) approach is chosen
