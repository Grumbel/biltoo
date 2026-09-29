# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.5-vips-concurrency-1 (base 2085c07).

### 2815.5
- ImageLoader::init: vips_concurrency_set(1) right after VIPS_INIT
- Stops default libvips thread-pool explosion before thumtoo image_library_init

### Prior
- 2815.4 stack linear docs
- 2815.3 formatLoadErrorMessage never exists

### Apply
```bash
git pull --ff-only …/biltoo-2815.5-vips-concurrency-1-2085c07.bundle HEAD
```

Fast-forward from origin tip 19816557 / 1cb81855 / 1149901.
