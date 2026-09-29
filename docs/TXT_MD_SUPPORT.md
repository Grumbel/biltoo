<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Text and Markdown support

Requires thumtoo ≥ **354.3** and MuPDF ≥ 1.28.

## Auto open

- **Markdown:** `.md`, `.markdown`, …
- **Plain text:** `.txt`, `.text`, and common sources/data (`.c`, `.h`, `.cpp`,
  `.py`, `.rs`, `.json`, …)

## Force type: `//text`

| Path | Effect |
|------|--------|
| `foo.xyz//text` | Expand as plain text via MuPDF |
| `foo.xyz//text//page:2` | Page 2 only |

Use when the extension is unknown or wrong.
