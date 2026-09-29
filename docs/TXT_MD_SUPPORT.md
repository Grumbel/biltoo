<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plain text and Markdown support

Status: **Markdown + plain text open/expand in biltoo** (2026-09-29). Requires
thumtoo `PathKind::Markdown` / `PlainText` and MuPDF ≥ 1.28 (`pinMupdf`).

Related: thumtoo `docs/MARKDOWN.md`.

---

## Markdown

- Extensions: `.md`, `.markdown`, `.mdown`, `.mkd`
- `PagePath::isMarkdownFile` → `expandMarkdownToPageRefs` → `file.md//page:N`

## Plain text

- Extensions: `.txt`, `.text`
- `PagePath::isPlainTextFile` → `expandPlainTextToPageRefs` → `file.txt//page:N`

Both use MuPDF’s native document handlers (reflowable page layout at defaults).

---

## Non-goals

- Live editing  
- Opening arbitrary source files (`.py`, …) without a text extension  
- Full GFM beyond MuPDF
