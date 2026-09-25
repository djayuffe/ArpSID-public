# 0.0.651-pass291 — C64/SIDCORE mirror/reset completion release

This release continues the C64/SIDCORE correctness integration after pass290.

## Closed

- Physical SID mirror decode is now preserved under multi-SID configuration.
  Exact configured SID bases still win first, but unassigned `$D400-$D7FF`
  mirror windows fall back to the primary SID (`$D400`) exactly as a stock C64
  SID decode does.
- This specifically prevents primary `$D418` digi mirror writes from being
  dropped when secondary SIDs are configured at `$D420`, `$D500`, etc.
- `C64SidBridgeState::reset()` now clears stale timed-write entries, not only
  counters. This prevents old `$D418` diagnostics/export state from being
  observable after a reset boundary.

## Tests

- Added `c64_sid_mirror_multisid_v713_tests.cpp`.
- Added `c64_sid_bridge_reset_stale_v714_tests.cpp`.
