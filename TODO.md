# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2413-tilelod-climb-complete-loop** (base `d80d461`).

### tilelod climb_to_scale
Trailing `issue_requests` without `complete_all` left scale-0 keys InFlight;
next step saw n=0 and early-returned → exact==0 / shared-cache failures.

**Fix:** issue → complete → drain pump in a loop until nothing left to issue.

Verified: `tilelod_test: all passed` (standalone compile).

### Apply
```bash
git pull --ff-only …/biltoo-2413.1-tilelod-climb-complete-loop-d80d461.bundle HEAD
```

**Next:** RC smoke; VERSION 0.2.0 + tag.
