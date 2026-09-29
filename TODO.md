# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2816.1 on origin/master (89e690b8).

### 2816.1
- Restore default libvips concurrency (drop vips_concurrency_set(1))
- Warm-cache settle was slowed by the cap; CPU spam was PreferCache→FocusFull

### Required thumtoo
thumtoo-001-prefercache-no-focusfull-551a360.bundle → ae7f722

### Apply
```bash
git pull --ff-only origin master
git pull --ff-only …/biltoo-2816.1-restore-vips-concurrency-89e690b.bundle HEAD
```

