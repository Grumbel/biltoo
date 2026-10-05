# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.10-denser-issue-missing (linear stack on `a889409`).

### 2892.10
- Backoff denser only on all-Failed; never when miss>0 (was aborting denser
  before issue → miss=N inflight=0 forever).

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
