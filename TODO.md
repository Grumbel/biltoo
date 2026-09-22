# TODO / agent handoff

## Status (2026-09-22)

**Tip: biltoo-2313-pathraster-noteDelivery-iterator.**

Fix crop-time SIGABRT: PathRasterService::noteDelivery held a QHash
iterator across emit rasterImproved; crop suspend / focus ensure can
cancel or rehash m_state and leave the iterator invalid (Qt QHash
node assert). Snapshot climb state, drop the iterator before emit,
re-find before pump.

Stacks on **biltoo-2312** (`999be36` on origin/master). Requires **thumtoo-320**.

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-320-activity-batch-warm-finish-8ea52ea.bundle HEAD
# if not yet on 2312:
git -C biltoo pull --ff-only …/biltoo-2312…bundle HEAD   # or already origin/master @ 999be36
git -C biltoo pull --ff-only …/biltoo-2313.2-pathraster-noteDelivery-iterator-999be36.bundle HEAD
```

Next: **2314**.
