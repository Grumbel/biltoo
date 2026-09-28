# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2738.1-cmake-system-includes` (base `636e70e`).

### Spread P0–P4 done; build hygiene

- **2737.3:** TextSelection metatype outside include guard (build break)
- **2738.1:** Mark Qt / pkg-config dependency includes as SYSTEM (`-isystem`)
  so GCC does not emit warnings from third-party headers (e.g. Qt
  `qarraydataops.h` `-Wstringop-overflow`). **thumtoo is not marked** — still
  our library diagnostics.

### Apply
```bash
git pull --ff-only …/biltoo-2738.1-cmake-system-includes-636e70e.bundle HEAD
```
