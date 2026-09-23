# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2348-release-0.2.0-src-layout.**

[docs/RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md):

- **§3a Source tree layout is in scope for 0.2.0** (tag may slip a few days).
  Move flat `src/` into domain dirs (`session/`, `crop/`, `gallery/`, `shell/`, …)
  following `tilelod/`; one domain per commit; no behaviour change.
- Known gaps §§4.1–4.8 still apply (Find, cold open, PDF embeds, Location, crop RC, menus).
- Dual ImageView remains **0.3.0**.

Requires **thumtoo-323**. Code tip includes 2318–2343.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2348.1-release-0.2.0-src-layout-2f201f6.bundle HEAD
```

Next: **2349** — start `src/session/` move (or RC crop smoke).

## Backlog (0.2.0)
- [ ] Source subdirectories (§3a): session → crop → gallery → shell → (display)
- [ ] RC smoke incl. crop + Open Selection
- [ ] VERSION 0.2.0 + tag

## Backlog (post-tag / 0.3)
- See RELEASE_0.2.0.md §§4.1–4.8
