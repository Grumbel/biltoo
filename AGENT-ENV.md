# Agent environment notes

## Project requirements

- **Qt ≥ 6.9** (CMake `find_package(Qt6 6.9 …)`; `QImage::flipped`).
- Primary integration build: **Nix flake** (`nix develop` / `nix build`).
- Optional: system CMake + Qt when packages are new enough.

## Runtime and debug environment variables

Application and thumtoo client knobs (debug traces, concurrency, cache paths)
are documented in **[docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)** and the
installed man page (`man biltoo`).

Dev-shell helpers only (`nix develop`): `BILTOO_SOURCE`, `BILTOO_BUILD_DIR`,
`THUMTOO_SOURCE_DIR`, `CMAKE_BUILD_TYPE`, `QT_PLUGIN_PATH`, `XDG_DATA_DIRS` —
same doc.

## Sandbox expectations

Automated agents often:

- **Cannot** run a full `nix build` of biltoo with the user’s flake inputs.
- **Can** clone from GitHub, edit, commit, and produce **git bundles**.
- Should place bundles under a shared artifacts directory when the platform
  exposes one (example: `/home/workdir/artifacts/biltoo-NNN-….bundle`).

Do not require Ubuntu Noble’s stock Qt 6.4 for project builds; the project
minimum is 6.9 via nixpkgs.

## Related tips (session 2026-09-09)

See biltoo tip **353** and thumtoo tip **121** for the end-of-session stack.
