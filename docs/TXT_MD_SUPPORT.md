<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plain text and Markdown support

Status: **Markdown open/expand in biltoo** (2026-09-29). Requires thumtoo with
`PathKind::Markdown` and MuPDF ≥ 1.28 (`pinMupdf`).

Related: thumtoo `docs/MARKDOWN.md`, `PathKind` / expand.

---

## Markdown (shipped path)

1. thumtoo classifies `.md` / `.markdown` / `.mdown` / `.mkd` → MuPDF `//page:N`
2. biltoo: `PagePath::isMarkdownFile`, session expand via
   `ThumtooCache::expandMarkdownToPageRefs`, open dialog filters

Same page-ref form as PDF (`notes.md//page:1`).

---

## Plain `.txt`

Still open: synthetic HTML vs future MuPDF text docs. **Not** implemented.

---

## Non-goals (still)

- Live editing of `.md` in biltoo  
- Full GFM fidelity beyond MuPDF’s engine  
- PDF write-back of text notes
