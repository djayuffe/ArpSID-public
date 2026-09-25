# ArpSID v902 — mono fractional timing + final drift closure

Date: 2026-07-07

## Fixed

- **P0 mono/shared-output fractional bypass.** `renderSlice_()` now finalizes fractional single-sample audio for mono/shared AU output buses too. The old `!sharedOutputBus` guard could let mono layouts fall through to canonical rendering after sub-cycle spans had already advanced the backend, creating a stereo/mono timing divergence and possible one-sample double-advance.
- **Late fractional mirror timing normalization.** The v898 fractional queue consumer now mirrors the same normalized sample/cycle timing that audio consumed: stale writes are applied at the current sample, cycle 0; unresolved sample-only writes mirror as cycle 0.
- **Subphase overflow telemetry lifetime.** `SidRegisterEngine::resetIntervalCursor()` no longer clears overflow/coalesced/drop counters per fractional sample. Telemetry is now reset explicitly with `resetSubphaseWriteTelemetryForBlock()`.
- **Equal-priority subphase pressure policy.** When the subphase queue is full, newer same-register equal/lower-priority writes replace the oldest same-register entry before dropping the incoming event. Gate-off/test/gate-on remain protected by the existing priority tiers.
- **Host timestamp mapping.** `canonicalHostSampleOffsetFromSeconds()` now floors with a tiny epsilon instead of nearest-sample rounding, avoiding future-sample pushes and ±0.5-sample jitter around tight gate boundaries.
- **Preflight false-green guard.** Full validation scripts now configure with `-DARPSID_BUILD_TESTS=ON` and use `ctest --no-tests=error` for full-suite runs.

## Tests

- Added `ProjectionMirrorFinalClosureV902Tests` covering:
  - late fractional audio/mirror timing parity,
  - unresolved sample-only cycle-zero mirror parity,
  - subphase telemetry persistence across cursor reset,
  - same-register equal-priority overflow replacement,
  - floor timestamp policy,
  - mono/shared fractional source-shape and preflight guards.
- Updated `C64ProjectionMirrorBehaviorV899Tests` to assert normalized v902 mirror timing metadata.

Focused closure sweep verified: `ProjectionMirrorFinalClosureV902|C64Projection|SidProjection|SidRuntime.*(SampleOnly|SameSample|Unresolved|MixedUnresolved)|SidWriteQueueRebase` — 22/22 PASS.
