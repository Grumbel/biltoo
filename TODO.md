# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.11-probe-memo-copy (base 2085c07).

### 2815.11
- scheduleProbe(Batch): size process memo alone skips Store request_size
  (was also requiring ImageCache underlay → full-session re-probe on hot SQLite)
- Performance panel: Copy report, Size probes card, intent narrative
- probeQueueSnapshot() for FIFO depth

### Apply
```bash
git pull --ff-only …/biltoo-2815.11-probe-memo-copy-2085c07.bundle HEAD
```

