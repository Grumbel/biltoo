# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2891.2-open-with-includes (linear stack on `origin/master` / `a889409`).

### 2891.2
- Fix openwith.cpp includes: `host/archivepath.h`, `host/pagepath.h`,
  `shell/openwith.h` (match tree-wide include style).

### 2891.1
- **Open With…** (File menu + canvas context menu): list *all* XDG-associated
  applications for the current page/image (not only the default), using GIO
  when built with `BILTOO_HAVE_GIO`, else Qt + mimeapps.list / desktop scan
  (same idea as dirtoo’s `open_with`).
- Session paths resolved to an openable local file: page refs → document
  container; archive members → archive file; plain paths as-is.
- **Open Containing Folder** opens the parent directory in the default file
  manager.
- Deferred app enumeration on submenu `aboutToShow` so right-click stays
  responsive.

### Bundle policy
Work-line base: `a889409` (`origin/master` at start of this sequence).
Full stack `a889409..HEAD`.
