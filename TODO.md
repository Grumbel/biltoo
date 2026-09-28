# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2741.1-qt-isystem-before` (base `636e70e`).

### 2741.1
- Stronger Qt SYSTEM includes: `SYSTEM BEFORE` on `biltoo_lib` so `-isystem`
  wins over `-I` (GCC only suppresses dep-header warnings then)
- Avoid `QVector(n, -1)` sized fill ctor in finishRubberBand (triggers the
  Qt `qarraydataops` `-Wstringop-overflow` false positive)

**Reconfigure required** after pull (`biltoo-configure` / re-run cmake).

### Apply
```bash
git pull --ff-only …/biltoo-2741.1-qt-isystem-before-636e70e.bundle HEAD
```
