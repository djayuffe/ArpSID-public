# ArpSID v908 — Phase2 no-output FX contract closure

This release closes the remaining Phase2/VST3 no-output timing/music parity gap found after v907.

## Fixed

- Phase2/VST3 no-output processing now runs the full scratch post-render contract:
  - canonical/fractional render to scratch buffers
  - render-mode normalization
  - post-FX / HiFi / reverb / limiter / dezipper state advancement
- Removed the stale `renderAudio(ProcessData&)` alternate render entrypoint. Standard Phase2/VST3 audio must use `processCanonicalBlockPhase2_()` so fractional reset, live glide scheduling, future-write rebase, no-output scratch drain, mono folding, normalization, and post-FX state advancement stay under one contract.

## Tests

- Added `Phase2NoOutputFxClosureV908Tests`.
- Updated the timing/music contract sweep to build and run the v908 test.
- Widened older lineage guards to accept v908 while still pinning their original contracts.
- `scripts/run_timing_music_contract_sweep.sh`: 29/29 PASS.
