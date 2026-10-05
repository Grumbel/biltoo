# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2891.6-open-with-stable-menu (linear stack on `a889409`).

### 2891.6
- Open With submenu stayed on "…" after a brief app flash: `updateStatus` →
  `populateMenu` cleared a live submenu. Rebuild only on path change /
  when not visible; `aboutToShow` fills from the path property.

### 2891.1–2891.5
- Open With feature, includes, QMenu, warning guards.

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
