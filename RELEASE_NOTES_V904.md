# ArpSID v904 — Final timing/music release closure

This pass closes the release-contract layer around the v903 timing/music fixes.
No intentional audio-law change is introduced in v904; the pass pins the v903
runtime contracts with an additional regression test and removes stale release
identity drift.

## Closure points

- Added `ProjectionMirrorFinalClosureV904Tests`.
- Added multi-block `SidWriteQueue` survivor regression coverage:
  - a future write at `sampleOffset > frameCount` remains future after one
    block rebase;
  - it becomes due in a later block;
  - it consumes exactly once and is erased.
- Added same-sample survivor ordering coverage after block-tail rebase.
- Pinned release scripts so tests are built and zero-test preflight is fatal:
  - `-DARPSID_BUILD_TESTS=ON`
  - `ctest --no-tests=error`
- Updated release identity to v904 so source tree, status, notes, manifest, and
  zip root no longer disagree.

## Audio/runtime status

v904 keeps the v903 timing laws:

- block-local delayed SID writes rebase in the fractional consumer;
- mono/shared output uses the fractional finalizer rather than falling back to
  canonical double-render;
- synth glide emits real SID frequency writes;
- late mirror timing is normalized to the audio-consumed timing;
- subphase telemetry is block/audit-window scoped;
- SIDChip cycle budgeting uses the dispatcher-compatible nominal PHI2 law.
