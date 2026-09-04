# Licensing (REUSE)

Biltoo follows the [REUSE specification](https://reuse.software/).

## Project license

Unless a file says otherwise, the project is licensed under **GPL-3.0-or-later**.

- Full text: [LICENSE](LICENSE)
- Bulk coverage for the tree: [.reuse/dep5](.reuse/dep5)

## Source and build files

C/C++ sources, CMake, and Nix files carry SPDX headers:

```text
// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
```

(or `#` comments where appropriate).

## Documentation and agent notes

User and developer markdown without in-file SPDX headers (for example `README.md`, `AGENTS.md`, `TODO.md`, this file) is covered by `.reuse/dep5` under the same copyright and GPL-3.0-or-later.

## Other exceptions

Individual files may use a different SPDX identifier (for example `CC0-1.0` for `.gitignore`). Those statements, or the corresponding entries in `.reuse/dep5`, are authoritative.

## Contact

Ingo Ruhnke \<grumbel@gmail.com\>
