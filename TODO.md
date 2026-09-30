# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2854.1-perf-working-stale (on `ea477d6` + agent stack).

### 2854.1
- Performance "Working" stuck: call thumtoo `reconcile_activity_if_idle()` from
  `workActivity()`; badge ignores orphaned `tileQueued` when host queue is idle.
- Requires thumtoo tip with `Client::reconcile_activity_if_idle` (012.1).

### Prior
- 2853.1 topic toolbars
- 2852.1 QSet cbegin

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
