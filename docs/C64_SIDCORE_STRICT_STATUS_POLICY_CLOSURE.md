# PASS301 — C64/SIDCORE strict status policy closure

This pass closes the P0 status-policy bug found after pass299/pass300:
compatible-mode RSID PHI2 execution must never be reported as a strict PHI2 path.

## Changes

- `C64Runtime::rsidStrictPhi2PathActive()` now requires `RsidPlaybackMode::Strict` in addition to the RSID PHI2 path being active.
- Added `C64Runtime::rsidPhi2PathActiveAnyMode()` for the old path/capability meaning.
- Default runtime RSID policy is now `Strict`; compatible mode remains opt-in via `setRsidPlaybackMode(RsidPlaybackMode::Compatible)`.
- AU PSID/RSID load path explicitly sets RSID playback mode to `Strict` for RSID loads before init/render telemetry is published.
- GUI badge text no longer says `RSID-EXACT`; it now says `RSID PHI2 CLEAN` for strict PHI2 + no known downgrade, and `RSID-PHI2 DOWNGRADE` when the exactness ledger has downgrade bits.

## Regression test

Added `source/tests/c64_strict_status_policy_v728_tests.cpp`.

It verifies:

- default runtime policy is strict, not compatible;
- default strict RSID reports a strict PHI2 path only after actual PHI2 init;
- compatible-mode PHI2 can be observed with `rsidPhi2PathActiveAnyMode()` but does not produce strict status 1/2;
- compatible PHI2 path is never physically exact.

## Honest remaining limitations

This pass fixes the strict-status trust bug. It does not make CIA, VIC-II, SID readable-register state, HLE ROMs, or deterministic open-bus behavior transistor/cycle exact. Those remain exactness-ledger downgrades until implemented physically.
