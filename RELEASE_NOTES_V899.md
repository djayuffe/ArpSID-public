# ArpSID v899 — C64 SID Projection Mirror Split-Brain Closure

## Fixed

- Normalized `kSidUnresolvedCycleOffset` (`0xFFFF`) in the C64 projection bridge so sample-only projection writes map to the host sample boundary instead of scheduling a bogus +65535 PHI2 delay.
- Added a projection-mirror flush path that applies pending projection mirror values through the normal C64 bus application path before removing them. This preserves the v885 block-local/no-late-replay law while preventing capped same-block writes from vanishing.
- Switched consumed and unconsumed telemetry mirror cleanup paths in `ArpSIDDSPKernel` from destructive clear to value-preserving flush.
- Mirrored writes consumed by the v898 fractional SID-register transfer path through `runtimeMirrorAppliedProjectionWrite`, restoring the v874 applied-write observer contract on the live fractional consumer.

## Tests

- Added `C64ProjectionMirrorBehaviorV899Tests` for:
  - unresolved-cycle sentinel normalization,
  - late same-block write value landing under the 768-cycle mirror cap,
  - no late replay after flush.
- Updated v884/v885 source-shape tests to resolve source paths robustly and to assert the new flush contract.

## Verified

- `ctest -R 'C64Projection|SidProjection' --output-on-failure` passes: 14/14.
- Manual v898 kernel audibility compile/run passes: `kernel_e2e_audibility_v898_tests PASS`.

## Final audit hardening

- Updated the fractional-render commentary so it no longer claims the C64 observer lives only on the legacy queue-render path.
- Extended `C64ProjectionMirrorBehaviorV899Tests` with a source guard that pins the v898 fractional transfer consumer to call `runtimeMirrorAppliedProjectionWrite()` with the original sample/cycle metadata.

## v900 final audit closure

- Normalized `kSidUnresolvedCycleOffset` inside the synth-mode delayed-write cycle helpers too. The sentinel is now consistently cycle 0 across audio transfer, C64 projection mirror, and hard-restart/re-gate delay math.
- Extended `SidRuntimeSampleOnlyPriorityV892Tests` to pin one-cycle delayed synth writes from a sample-only base at sample 0/cycle 1 rather than the host-sample tail.
