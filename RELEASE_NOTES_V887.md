# ArpSID v887 — Fractional Unresolved Clock Closure

Final clock-authority closure after v886.

## Fixes

- Removed the unresolved-event fast render law for fractional-capable backends.
  Unresolved MIDI/UI events are still applied at sample 0, but the backend now
  advances through the same per-sample SidCycleClockState law used for resolved
  cycle events.
- Preserved the non-fractional backend fast path.
- Fixed a duplicate `liveLatch` declaration in the C64 SIDPLAY chunk cadence
  preflight path.

## Tests

- Added `SidRuntimePhysicalClockLawV887Tests`, a behavioral dispatcher test that
  proves unresolved-only events on fractional-capable backends use physical
  per-sample spans and apply before the first span.
- Updated the v886 source test to lock the unresolved/fractional contract.
