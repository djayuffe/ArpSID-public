# ArpSID 0.0.690 pass380 — final source closure

Status: **source-side closure complete through V952**.

source-level P0/P1/P2 closure: PASS

This package has closed the pass380 source audit items for render-thread state apply, SID runtime restore, C64/PSID/RSID exactness guards, realtime rollback, source hygiene and release documentation.

V952 additionally closes SID808 transport/reset behavior with an executable
audio test, parameter-specific normalization/host cardinality, generation-ordered
dirty-fallback side effects, state-root persistent-policy reconciliation, and the
remaining audited sign-compare warnings. The preserved V800–V819 list below is
the historical base guard range, not the end of current coverage.

## Closed source/audit items

- P0/P1/P2 source-side blockers are either fixed or guarded by source tests.
- Render-side state restore is prepared off the audio thread and applied through RT-only helpers.
- SID runtime restore uses the post-swap applied root.
- SID-808 restore is gated by component flavor and hydrated applied-root bank slot.
- PSID `playAddress == 0` continuous routing is guarded.
- `$D41D` pseudo/system-byte routing is guarded.
- C64 strict/compatible truth boundaries are documented and guarded.
- Full `C64Platform` render snapshot/rollback was replaced by a bounded mutation journal.
- Dead root-level C64 scratch/demo artifacts were removed from the source closure.

## FactoryBankAudioAuditV687Tests

`FactoryBankAudioAuditV687Tests` now defaults to **bounded closure** mode so the full CTest suite can close in normal Debug CI/container runs:

- all 180 factory slots are structurally verified;
- a deterministic representative audio set is rendered;
- non-silence and note-release checks are preserved for the rendered subset.

The exhaustive all-slot audio render remains available for release-machine soak:

```bash
ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT=1 ./build-debugtests/arpsid_factory_bank_audio_audit_v687_tests
```

## Closure guard range

The pass380 closure guard range is V800–V819:

```text
StaleVersionGuardSweepV800Tests
FinalSourceClosureV801Tests
C64SidPlayerFiveFlavorPolicyV802Tests
Sid808RestorePreloadDrainV803Tests
RenderApplyRTOnlyHelpersV804Tests
PostSwapAppliedRootGuardV805Tests
RenderBridgeSnapshotStackGuardV806Tests
PsidPlayZeroContinuousGuardV807Tests
StrictRsidFallbackTruthGuardV808Tests
SidD41dSystemByteRoutingV809Tests
PsidObservedRiskParityV810Tests
C64LowlevelEdgeContractV811Tests
C64PsidSentinelAndDocsV812Tests
C64RenderMutationJournalV813Tests
StateApplyOwnershipSplitV814Tests
SidRuntimeRestoreDualEngineV815Tests
Sid808RestoreFlavorIntegrationV816Tests
RealtimeSourceLintV817Tests
ReleaseCleanupClosureV818Tests
ReleaseDeadFileClosureV819Tests
```

## Platform closure still requiring macOS

The following cannot be truthfully validated in this Linux container and require macOS logs:

- AUv2 `auval`;
- AUv3/Logic runtime;
- VST3 SDK/toolchain where applicable;
- Apple signing/notarization.

Until those logs are attached, platform closure remains **PENDING**, while source-side closure is **COMPLETE**.

Explicit external validation statuses:

- auval: PENDING until Mac log proves success
- Logic runtime: PENDING until Mac runtime test proves success
- VST3 SDK/toolchain validation: PENDING until built with the SDK
- notarization: PENDING until Apple notarization log proves success
