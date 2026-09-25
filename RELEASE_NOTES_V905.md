# ArpSID v905 — Final timing/music verification closure

This pass does not change the v903/v904 runtime audio law. It closes the last
release-validation gap around that law: the focused timing/music contract sweep
is now a first-class script that builds every selected test executable before
running CTest.

## Added

- `scripts/run_timing_music_contract_sweep.sh`
  - configures with `-DARPSID_BUILD_TESTS=ON`
  - explicitly builds the timing/projection/rebase/regression test targets
  - runs CTest with `--no-tests=error`
- `ProjectionMirrorFinalClosureV905Tests`
  - pins the v905 release identity
  - verifies the new sweep script builds the selected targets before CTest
  - verifies the sweep cannot pass with zero tests

## Runtime status

v905 keeps the v903/v904 runtime timing laws:

- fractional mono/shared-output finalization is active
- future-block SID writes are rebased by the fractional consumer
- late mirror timing is normalized to audio-consumed timing
- synth glide emits register writes in the AU3 live render path
- preflight/closure validation cannot pass with zero tests
