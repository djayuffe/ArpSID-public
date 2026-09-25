# ArpSID v900 — Projection Mirror Final Audit Closure

## Fixed

- Normalized `kSidUnresolvedCycleOffset` inside synth-mode delayed-write cycle helpers.
- Closed the last sentinel split between audio transfer, C64 projection mirror, and synth-mode hard-restart/re-gate delay math.
- Sample-only delayed writes now schedule from physical cycle 0, not the host-sample tail.

## Tests

- Extended `SidRuntimeSampleOnlyPriorityV892Tests` to pin one-cycle delayed synth writes from a sample-only base at sample 0 / cycle 1.
- Kept the v899 mirror tests covering unresolved-cycle normalization, capped-mirror value flush, and no late replay.

## Verified

- `ctest -R 'C64Projection|SidProjection|SidRuntime.*(SampleOnly|SameSample|Unresolved)|SidWriteQueueRebase' --output-on-failure`
- Result: 21/21 PASS.
