# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2731.2-spread-docs-status` (base `636e70e`).

### Spread P0+P1 — complete

| Item | Status |
|------|--------|
| SpreadState / SpreadBook / layoutSpread | done |
| Double view (Ctrl+2), nav stride | done |
| Multi-underlay install (no wipe on soft/full) | done (2729.1) |
| View Selection + Gallery context menu | done |
| Status `Spread a–b/N` + prev/next tips | done (2730.1) |
| Event-driven layout sync | done (2730.1) |
| FixedN filmstrip moves window | done (2730.1) |
| Single Gallery open / Dual compare clear spread | done (2731.1) |
| Spread clears Dual; N≤8 cap | done (2731.1) |
| Text overlay deferred while N>1 | done (2731.1) |

### Next (P2+)
- Cross-page text selection / copy (SpreadTextCoordinator)
- TTS spans across members
- Binding hints UI, RTL, N>2 polish

### Apply
```bash
git pull --ff-only …/biltoo-2731.2-spread-docs-status-636e70e.bundle HEAD
```
