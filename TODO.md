# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2378-todo-03-imageview-ownership.**

**Source layout complete for 0.2.0** (phases 1–31).

Root is façade-only: `imageview*`, `imageitem*`, `imageview_types.h`, `main.cpp`.
All other translation units live under domain dirs (17 domains + tilelod + util).

Static verify: 0 unprefixed domain includes; cmake OK except generated `version.h`.

**Next (release path — stop layout moves):**
1. RC smoke (open, Gallery, crop, export, shell icons after `.qrc` move)
2. Pin thumtoo ≥ 323 in flake.lock
3. VERSION 0.2.0 + tag

Requires **thumtoo-323**.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2378.1-todo-03-imageview-ownership-8d5061c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–31 (layout complete)
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag

## Backlog (0.3.0)

### ImageView ownership extraction (planned)

Do **not** dump `imageview_*.cpp` into a folder without transferring ownership.
Continue the Phase 5 controller pattern: narrow Host API + collaborator owns
state; `ImageView` remains the public QGraphicsView surface.

**Candidates still on ImageView (extract in order of risk/value):**

| Collaborator (working name) | Pull from | Notes |
|-----------------------------|-----------|--------|
| Paint / overlay host | `imageview_paint*`, background overlays | Orchestration only; thumtoo keeps pixel stamps |
| Input router | `imageview_input*` | Dispatch + mode gate; policies already elsewhere |
| Session bind/remove | `imageview_session_*`, size book | Identity rules stay in IDENTITY.md |
| Rematerialize / bake host | `imageview_rematerialize`, `imageview_bake` | Display pipeline remains display/ |

**Optional small win (any time if interaction bugs surface):** split
`imageitem_interaction.cpp` (~1.8k lines) into handle-drag / rubber-band /
content-anchor TUs — still `ImageItem`, no dir move required.

**Also 0.3 product:** dual ImageView / two-up compare — [RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md) §4.8.
Ownership extraction reduces the cost of that work.

See [REFACTOR.md](REFACTOR.md) (post–Phase 5) and [docs/SRC_LAYOUT.md](docs/SRC_LAYOUT.md).
