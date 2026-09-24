# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2412-tilelod-climb-drain-pump** (base `d80d461`, includes 2403–2411).

### tilelod_test failure (target_scale 1 != 0)
`TileSession::pump` caps at 16 completions/call (GUI budget, 2409).
`climb_to_scale` issued/completed a full batch then **pumped once**, leaving
most keys `InFlight`. Progressive climb never reached scale 0 on a 4096²
viewport (256 cells at scale 0).

**Fix:** Drain `pump()` until empty in `climb_to_scale`; raise max_steps to 48.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2412.1-tilelod-climb-drain-pump-d80d461.bundle HEAD
```

**Next:** RC smoke; VERSION 0.2.0 + tag.

## Prior — 2411
MasonryFill global fit to avail after column equalization.
