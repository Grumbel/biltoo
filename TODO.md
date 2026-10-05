# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.12-budget-zero (linear stack on `a889409`).

### 2892.12
- Root cause from IssueDiag: `early=2 bud=0` — issue_requests(0) never
  requested denser tiles. Pump-only when budget<=0; debug after issue;
  Image share up to full budget.

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
