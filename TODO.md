# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `biltoo-2813.1-markdown-open` (base `2085c07`).

### 2813.1
- PagePath::isMarkdownFile + markdownSuffixes
- Session expand + expandMarkdownToPageRefs (MuPDF page count)
- Open dialog filters; TOC accepts markdown docs
- Needs thumtoo **354.1** (PathKind::Markdown) + MuPDF 1.28.5 pin

### Prior
- 2812.1 MuPDF pin in biltoo flake
- Tool unification 2811.x

### Next
- Manual: open a `.md` in biltoo (THUMTOO_SOURCE_DIR with 354.1)
- Optional: .txt path

### Apply
```bash
# thumtoo first if not already:
git -C thumtoo pull --ff-only …/thumtoo-354.1-markdown-pathkind-fb6a408.bundle HEAD
git pull --ff-only …/biltoo-2813.1-markdown-open-2085c07.bundle HEAD
```
