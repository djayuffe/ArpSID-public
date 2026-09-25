# ArpSID v906 — Phase2/VST3 timing/music parity closure

This closure ports the AU3 v903/v904 timing contract to the Phase2/VST3 runtime path.

## Fixed

- Phase2/VST3 now routes float, double, and no-output processing through one canonical block helper.
- The helper resets fractional accumulators at block start, schedules live synth-mode glide writes, runs canonical dispatch, and calls `runtimeEndFractionalBlock()` at block end.
- Future-block delayed SID writes in VST3 are rebased at block boundaries, preventing stale block-local offsets from causing stuck TEST/gate writes or dead voices.
- No-output/inactive-host processing uses scratch output buffers so fractional finalizers drain and slice-owned controller advancement still occurs.
- Mono VST3 render now folds L/R SID output instead of losing right-channel fractional energy.
- Double-precision mono VST3 post-FX now runs as shared mono instead of treating a zero right scratch buffer as real stereo.
- Early process precondition returns clear host output buffers.

## Verification

- Added `Phase2TimingMusicClosureV906Tests`.
- Updated `scripts/run_timing_music_contract_sweep.sh` to build/run v906 Phase2 closure tests and include v906 lineage in the CTest regex.
