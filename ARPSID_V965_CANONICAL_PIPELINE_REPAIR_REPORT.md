# ArpSID v965 canonical render-pipeline repair report

Package identity: `0.0.690-pass380-v965-canonical-render-pipeline-closure`

## Scope

This repair pass targets the complete render chain audited in the preceding focused review:

`canonical timed events -> processBlock()/processCanonicalBlockPhase2_() -> runtime model -> SidHostCycleDispatcher -> sample/cycle/subphase slicing -> BitPerfect/SID-register/DrSID/SID808 -> KIT/DIGI overlays -> MIX/reverb/limiter/Hi-Fi -> GUI telemetry snapshots`

The implementation is source-complete for the audited Linux-buildable paths. Platform sign-off for AUv2/AUv3 in Logic, Apple signing/notarization, and a build against the real Steinberg VST3 SDK remains external validation work and is not represented as completed here.

## Closure matrix

| Audit finding | Repair | Verification |
|---|---|---|
| Two competing same-sample sort laws | `SidTimedEvent::before()` now preserves canonical `arrival_order` after emergency-kill and physical cycle/subphase rules. Event-type priority is only a malformed-token fallback. | `CanonicalRenderPipelineClosureV965Tests`, `IngressParityTimingAuthorityV910Tests`, `SidRuntimeSameSamplePolicyV891Tests` |
| Release events could be excluded by a full queue | Fixed-capacity headroom is reserved for NoteOff, AllNotesOff, AllSoundOff, Panic and transport boundaries. A higher-priority incoming event can replace lower-priority traffic at saturation. | v965 release-reserve/overflow tests |
| Queue replacement left stale cycle metadata | Every replacement recomputes `hasResolvedCycleTimingFlag`. | v965 cycle-metadata test |
| ARP and fractional BitPerfect had separate timing authorities | Render-local ARP simulation replays canonical MIDI/automation at exact offsets, appends marked internal gates to the same queue and installs the exact final ARP state after dispatch. Fractional slicing stays enabled. | `ArpAudioRenderV954Tests`, `ArpGateOffTimingV961Tests`, v965 same-block ARP test |
| Structural mode changes could split one host sample between engines | Render-mode, SID model and PAL/NTSC changes make the transition block use sample-boundary slicing. | v965 structural-boundary test, `RenderModeTransitionKitPreservationV942Tests` |
| Dispatcher could keep old Q32 clock after PAL/NTSC automation | Structural clock events bypass fractional cycle slicing for that block; subsequent blocks configure Q32 from the new physical clock. | v965 structural-clock test, `FractionalSmoke` |
| KIT/DIGI inferred one trigger from block-final sequencer state | `SequencerEngine::advanceWindow()` now publishes every `{stepIndex, sampleOffset}` boundary. The same list drives KIT, float DIGI and authentic D418. | v965 multi-boundary test, `SeqSwingTempoV571Tests`, `KitMixedTargetRuntimeV650Tests` |
| Post-FX used block-final values before their event offset | A shared post-FX automation state is captured at block start. Reverb/limiter are applied sample-by-sample and Hi-Fi in exact contiguous event segments. | v965 post-FX test, `Phase2NoOutputFxClosureV908Tests` |
| AU no-output path froze the runtime | AU renders the complete pipeline into fixed preallocated stereo scratch, including oversized parent-block chunking, then discards only the samples. | v965 no-output continuation test |
| AU GUI read live non-atomic render fields | DrSID mode, ARP/SEQ state and parameter presentation are render-published atomics under the telemetry generation protocol. | `TelemetryRuntimeCoherencyV745Tests`, v965 source/runtime contract test |
| AU scopes could be combined with a different scalar frame | DIGI, oscillator and C64 scope snapshots are copied only when `frameId == telemetryFrameId`. | v965 telemetry source contract |
| Authentic D418 was fixed to PAL | D418 prepare/retime uses the current active SID clock. `updateTimingPreserveVoices()` changes sample/PHI2 rates without killing active sample voices. | `DigiD418StreamEngineV698Tests`, v965 source contract |
| Phase2 dropped every host block larger than declared max | `setupProcessing()` preallocates bounded emergency capacity; normal transient oversize blocks continue through render, FX and telemetry. Blocks beyond actual fixed capacity are rejected before any block state is mutated. | v965 source contract |
| VariantChange mutated runtime twice | Canonical state application owns the runtime mutation; execution owns only concrete backend projection. | v965 source contract, `RuntimeBehaviorClosureV952Tests` |
| AU/Phase2 overlay parity was ambiguous | Compile-time capabilities explicitly declare AU KIT/DIGI/MIX support and Phase2 canonical-core/post-FX/no-output support. No wrapper may claim layers it does not instantiate. | v965 capability-contract test |

## Main implementation files

- `include/arpsid/core/sid_event_queue.h`
- `include/arpsid/core/sid_runtime_backend.h`
- `include/arpsid/core/sid_runtime_engine_ops.h`
- `include/arpsid/core/sid_runtime_execution.h`
- `include/arpsid/core/sid_runtime_model.h`
- `include/arpsid/core/sid_runtime_shared_kernel.h`
- `include/arpsid/core/sid_runtime_target_adapter.h`
- `include/arpsid/core/sid_postfx_timeline.h`
- `include/arpsid/core/sid_render_pipeline_capabilities.h`
- `include/arpsid/engines/digi_d418_stream_engine.h`
- `source/au3/ArpSIDDSPKernel.hpp`
- `source/au3/ArpSIDSequencerEngine.h`
- `source/arpsid_processor_phase2.h`
- `source/arpsid_processor_phase2.cpp`
- `source/tests/canonical_render_pipeline_closure_v965_tests.cpp`
- `scripts/check_version_coherence.py`

## Focused executable validation

The final merged source state passed these 18 focused tests:

1. `FractionalSmoke`
2. `DigiD418StreamEngineV698Tests`
3. `SeqSwingTempoV571Tests`
4. `KitMixedTargetRuntimeV650Tests`
5. `TelemetryRuntimeCoherencyV745Tests`
6. `SidRuntimeSameSamplePolicyV891Tests`
7. `Phase2NoOutputFxClosureV908Tests`
8. `IngressParityTimingAuthorityV910Tests`
9. `RenderModeTransitionKitPreservationV942Tests`
10. `StateRootStagingParityV951Tests`
11. `RuntimeBehaviorClosureV952Tests`
12. `ArpAudioRenderV954Tests`
13. `ArpGateOffTimingV961Tests`
14. `GuiUserKitAndPopoutWiringV962Tests`
15. `GuiA11yTooltipCoverageV963Tests`
16. `C64SidplayTelemetryV964Tests`
17. `CanonicalRenderPipelineClosureV965Tests`
18. `VersionCoherenceV965Tests`

All 18 passed against the final merged source. The source-only package contains 931 files and all 930 non-self manifest entries verify by SHA-256. Release/default optimization was used for the low- and medium-weight runtime set; the heavyweight duplicate-translation closure targets used `-O0` or `-O1` with `NDEBUG` to avoid recompiling the same large forensic unit at full optimization. Test logic and source inputs were unchanged.

## Release identity closure

The mixed v951/v952/v961 package identity is replaced with v965 across `VERSION.txt`, README, STATUS, TODO, AI2AI, source-only release documentation and release notes. `scripts/check_version_coherence.py` rejects a package where the declared revision, newest test revision, CMake registration, status and release notes disagree.

## Validation limits

- The full 479-test suite was not rebuilt and executed from a clean directory; the project creates many separate heavyweight test translation units and focused tests were selected around every modified contract.
- No AUv2/AUv3 host validation, `auval`, Logic session, code signing or notarization was possible in this Linux environment.
- The real Steinberg VST3 SDK was not available locally, so Phase2 was validated through the repository’s source/closure tests rather than a production VST3 binary.
- Full ASan/UBSan instrumentation of the largest AU kernel translation unit did not complete in the available environment. This report does not claim sanitizer closure.

These limits concern external/platform proof, not known open defects in the repaired audited pipeline.
