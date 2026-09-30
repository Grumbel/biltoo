# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2844.3-xdg-state-path (on origin `62bc891`).

### Bundle policy
Every tip bundle = `origin/master..HEAD` at creation time (all agent commits
since last upstream tip). After `git pull` of that bundle, next tip bases on
the new HEAD once it is origin, or on origin if only origin advanced.

### 2844.3
- SessionListStore::stateDirectory — pure XDG (`$XDG_STATE_HOME/biltoo`),
  no AppStateLocation.
