<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Agent / sandbox build environment

Notes for automated agents (and humans) working on this tree inside the
current remote sandbox. Read this before the first compile of a session.

## Host snapshot

| Item | Value |
|------|--------|
| OS | Ubuntu 24.04.4 LTS (Noble) |
| Arch | x86_64 |
| RAM | ~1.2 GiB (keep `-j` modest) |
| Disk | ~20 G overlay |
| Internet | yes |
| Persistent dir | `/home/workdir/artifacts` |
| Ephemeral work | `/tmp` (often wiped between turns) |

## Always available

- `cmake` 3.28.x
- `g++` 13.3
- `git` 2.43
- `pkg-config`
- basic coreutils / sed / grep / bash

## Not available by default

- **Nix** (`nix`, flakes, `/nix`, `nix shell`, `nix build`) — do not use the project flake here.
- `rsync` — required by hand-off rules; install it.
- `qt6-base-dev` and the rest of Qt 6 development packages.
- Optional native deps the flake would pull: `libvips`, `libexiv2`, `kimageformats`.

## Bootstrap (run once per fresh environment)

```bash
apt-get update -qq
apt-get install -y -qq qt6-base-dev cmake g++ pkg-config rsync
```

Verify:

```bash
pkg-config --modversion Qt6Widgets   # expect 6.4.2 on Noble
which rsync cmake g++
```

## How to build here (no Nix)

```bash
cd /tmp
git clone --depth 50 https://github.com/Grumbel/qimgview.git qimgview-src
# or: git pull previous bundle(s) onto a shallow clone
cd qimgview-src
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

CMake already tolerates missing libvips / libexiv2 (“using Qt image codecs only”).

## Qt version caveat

- Sandbox packages: **Qt 6.4.2**
- Project / nixpkgs: newer Qt (code uses `QImage::flipped`, which exists only since **Qt 6.9**)

A full link under this sandbox therefore fails on `.flipped(...)` until either:

1. a compatibility helper (`#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)` → `flipped` else `mirrored`) is added, or
2. verification is done only under the project’s Nix environment.

The LayoutMode declaration-order fix is independent of that API and is the compile error that appeared under Nix after Phase 3.

## Bundles & artifacts

- Work in `/tmp`, then:

  ```bash
  rsync -a /tmp/qimgview-00N-….bundle /home/workdir/artifacts/
  ```

- Prefer thin stacking bundles:

  ```bash
  git bundle create qimgview-00N-description.bundle <previous-tip>..HEAD
  ```

  First bundle after upstream tip `a194a10` was `a194a10..HEAD`.

- Bundle rules (project): continuous numbering, `HEAD` as ref, author `Ingo Ruhnke <grumbel@gmail.com>` + `Co-authored-by: Grok <grok@x.ai>`, small focused commits.

## Practical tips

- Re-run the `apt-get` bootstrap when `pkg-config --modversion Qt6Widgets` fails — the environment can be reset between turns.
- Keep intermediate trees under `/tmp`; only the final `.bundle` (and any requested docs) go into `artifacts`.
- Never assume Nix or Qt ≥ 6.9 in this sandbox.
