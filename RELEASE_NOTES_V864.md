# ArpSID 0.0.690 pass380 v864 Release Notes

Date: 2026-07-04

## Summary

v864 is the SID-core audit closure pass. It keeps the v863 SID-808 Logic/AUv2
audio-path fix and adds low-level SID parity fixes and guards for pulse-width
edge cases, TEST/noise reset, hard-restart timing, sync/ring timing,
filter/cutoff law, `$D418` master-volume DC behavior, subcycle timed writes,
and C64 readback parity.

## Fixed

- `$D418` volume-zero DC leak: revision calibration DC now lives before the
  DC/RC output stages and is owned by the master-volume DAC instead of being
  added as permanent post-volume output.
- Native interval cycle advance: `sidChipAdvanceCyclesNative()` now advances
  exact SID cycles instead of calling host-sample `processSample()`.
- Native subcycle advance: `sidChipAdvanceSubphasesNative()` now delegates to
  a direct SID subcycle lattice method rather than planned host-sample state.
- Native partial-interval rendering: `sidChipRenderIntervalNative()` now uses
  direct native subcycle advancement for leading/trailing partial cycles, so
  absolute cycle indices do not depend on a host-sample planner cursor.
- Waveform select compatibility: `SIDVoice::setWaveform()` accepts both
  internal waveform nibbles (`0x01`, `0x08`) and raw SID control-register
  waveform masks (`$10`, `$80`) instead of silently treating raw masks as
  waveform zero.
- C64 readback sync parity: `SidReadbackModel` now uses the same no-cascade
  hard-sync rule as `SIDChip`.

## Guard Coverage

- `SidCoreAuditV864Tests`
- `MultiSampleRateRenderFingerprintV529Tests`
- `SidCoreExactnessV854Tests`
- `C64PlayBridgeRoutingV855Tests`
- `ZeroCycleAndParamSmoothingV533Tests`
- `FilterCoreUnificationAndTopologyModeV532Tests`
- `C64SidReadbackPhysicalV711Tests`
- `C64CycleExactClosureV741Tests`
- `C64FinalCorrectnessV617Tests`
- `C64FullSidcoreIntegrationV708Tests`
- `C64SidcoreCompleteIntegrationV709Tests`
- `DigiD418StreamEngineV698Tests`
- `DigiD418SidVolumeDacV699Tests`
- `Auv2PureSidD418ZohContinuityV736Tests`

## Release Validation

- Full macOS closure passed: `release-logs/macos-closure-20260704-004703.log`.
- Release-check curated guards: 7/7 passed.
- Full CTest: 384/384 passed.
- Strict installed AUv2 verification passed for `ArpS`, `ArIn`, `DrSD`, `S808`,
  and `C64P`.
- Installed AUv2 binary SHA256:
  `fe08fbbf74ac4db795b77db039f2b20428e3768fc44bffc4e3956cfd37489a72`.

## Documentation

- Added `SID_CORE_AUDIT_CLOSURE_V864.md` with detailed closure notes for each
  SID-core audit area.

## Carried Forward From v863

- SID-808 canonical DrSID-mode MIDI routes through the kernel target hook.
- SID-808 bridge output replacement remains fail-open.
- C64 SIDPLAY branches early and keeps dirty-log sync consumable.
- AUv2 v864 installed validation supersedes the v863 installed binary.
