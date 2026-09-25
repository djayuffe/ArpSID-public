# ArpSID v965 validation record

Package: `0.0.690-pass380-v965-canonical-render-pipeline-closure`

## Package composition

- User baseline closures preserved: GUI wiring v962, accessibility/tooltips v963, C64 SIDPLAY telemetry v964.
- Canonical render-pipeline repair integrated as v965 without replacing those source changes.
- CMake configuration registers **479 tests**.
- Source-only package contains **931 files**, with **930 SHA-256 manifest entries** (the manifest does not hash itself).

## Focused dynamic validation

The following **18/18** tests passed against the final merged source:

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

The first low-/medium-weight set was built with the repository's normal Release configuration. Heavy closure executables that independently compile the very large forensic/kernel translation were built with `NDEBUG` and reduced optimization (`-O0` or `-O1`) to complete validation in the available 4 GiB Linux environment. No test source or runtime contract was weakened.

## Static/package guards

Passed:

- `scripts/verify_source_tree.py`
- `scripts/check_audit_closure.py`
- `scripts/check_version_coherence.py`
- `git diff --check`
- SHA-256 verification of all 930 release-manifest entries after packaging

## External validation boundary

This environment cannot prove macOS AUv2/AUv3 loading, Logic behavior, `auval`, Apple signing/notarization, Metal compilation, or a production VST3 build against the real Steinberg SDK. The v962-v964 baseline documents prior macOS validation claims; this repair pass does not independently reassert them as newly reproduced.
