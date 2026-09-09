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
pkg-config --modversion Qt6Widgets   # project requires ≥ 6.9 (nixpkgs)
which rsync cmake g++
```

## How to build here (no Nix)

```bash
cd /tmp
git clone --depth 50 https://github.com/Grumbel/biltoo.git biltoo-src
# or: git pull previous bundle(s) onto a shallow clone
cd biltoo-src
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

CMake already tolerates missing libvips / libexiv2 (“using Qt image codecs only”).

## Qt version caveat

- Project requires **Qt ≥ 6.9** (`QImage::flipped(Qt::Orientations)`).
- Sandbox/apt may still ship older Qt; use the Nix flake for a full build.
