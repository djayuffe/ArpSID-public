# ArpSID 0.0.690 pass380 v863 Release Notes

Date: 2026-07-03

## Summary

v863 closes the SID-808 Logic/AUv2 audio path that remained after v861/v862
analysis. The installed AUv2 component now validates with strict `auval` for all
five flavors, and the SID-808 subtype renders audible output through both the
direct AUAudioUnit render-event path and the installed AUv2 host path.

## Fixed

- SID-808 canonical drum MIDI now routes through the kernel target hook:
  `CanonicalRuntimeBackend::triggerDrumMidi()` calls
  `runtimeTriggerDrSidNote()` before any canonical DrSID fallback.
- SID-808 drum releases now route through `runtimeReleaseDrSidNote()`.
- Silent SID-808 bridge scratch no longer overwrites final output unless the
  bridge has renderable activity.
- C64 SIDPLAY cleanup remains edge-triggered and branches before normal
  synth/drum/MIDI work.
- C64 PHI2 SID-write observation no longer scans the dirty-write log, and
  consumed dirty-write logs are cleared after platform RAM sync.

## Guard Coverage

- Full macOS closure CTest: 383/383 PASS
- `Sid808TargetRoutedDrumMidiV862Tests`
- `DrumBridgeNoSilenceV613Tests`
- `DigiMidiPadTransportStopV745Tests`
- `C64FinalCorrectnessV617Tests`
- `C64RenderStallInstrumentationV840Tests`
- `Sid808RestorePreloadDrainV803Tests`
- `Sid808StrictAuSlotPolicyV861Tests`

## macOS AUv2 Validation

- Final closure log: `release-logs/macos-closure-20260703-234236.log`
- Direct AUAudioUnit SID-808 render-event smoke: PASS
- Installed AUv2 SID-808 smoke: PASS
- Strict installed AUv2 verification: PASS for `ArpS`, `ArIn`, `DrSD`, `S808`,
  and `C64P`
- Installed AUv2 binary SHA256:
  `af2ea5def5d37e1ce6054aee12d99621203a0f6c31ccb7484a00507c3ec50af7`

## Packaging

- Clean source packages exclude generated build trees, `dist/`, and
  `release-logs/`; release logs are retained beside the working tree for audit,
  not embedded in the source zip.

## Remaining External Gates

These are distribution/signoff tasks, not source-code blockers:

- Apple notarization
- VST3 SDK/toolchain validation
- AUv3 product packaging/runtime validation
