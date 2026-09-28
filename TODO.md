# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2728.1-spread-p0` (verified + secondary probe).

### Verify
- Bundle FF on origin/master
- StrictPairs / stride pure logic OK
- APIs: findItemForPath, hostSetPreviewImage, createPlaceholderItem
- Secondary pages: scheduleProbe when no cache pixels

### Manual check
1. Multi-page session → View → Double view (Ctrl+2)
2. Two pages side by side
3. Next steps by pair
4. Toggle off → single page

### Next
- Gallery View Selection (P1)
- Cross-page text (P2)
- Full secondary decode path
