# ArpSID 0.0.690 pass380 v865 Release Notes

Date: 2026-07-04

## Summary

v865 closes the remaining SID-808 observability and audibility gap after the
v861/v862 audio-path fixes. The bridge now proves that a hit was routed, a
SID-808 kit is configured, and the bridge produced measurable output. The UI
also shows the same audible authority instead of relying on generic DrSID drum
telemetry.

## Fixed

- SID-808 waveform conversion is boundary-safe. Kit data, GUI kit conversion,
  hit overrides, and final voice programming now accept both raw SID
  control-register waveform masks (`0x10`, `0x80`) and internal waveform
  nibbles (`0x01`, `0x08`).
- Malformed SID-808 audible configs no longer drop a valid internal waveform
  to waveform zero through `& 0xF0`.
- Every canonical SID-808 drum family is guarded with an audible-minimum bridge
  render regression: kick, snare, closed hat, open hat, clap, cowbell, tom, and
  rim must all route and render above the minimum peak/RMS threshold.
- `DrumEngineHostBridge` now publishes SID-808 bridge/output telemetry:
  configured kit slot, routed-hit count, last routed drum class, last routed
  MIDI note, last routed velocity, active SID-808 context, and output peak.
- AU kernel telemetry now promotes SID-808 bridge data to first-class snapshot
  fields and uses SID-808 engine drum levels when SID-808 owns the audio path.
- SID-808 bridge post-DC output peak and bus-replacement state are published
  from the render path.
- The AU view now has a unified audible-authority label. SID-808 displays show
  `AUTH SID808-BRIDGE`, kit number, routed hit count, bridge peak, and whether
  output was replaced or fail-open.
- Drum meters, pad activity, HUD glow, and RT LEDs include the SID-808 bridge
  output peak, so the UI cannot look alive only because canonical DrSID
  telemetry moved.

## Guard Coverage

- `Sid808AudibleAuthorityV865Tests`
- `Sid808TargetRoutedDrumMidiV862Tests`
- `DrumBridgeNoSilenceV613Tests`
- `Sid808EngineAndRouterV535Tests`
- `Sid808HitOverrideV626Tests`
- `KitSid808FactoryVoicePrecedenceV637Tests`
- `Sid808StrictAuSlotPolicyV861Tests`
- `DigiMidiPadTransportStopV745Tests`

## Validation

Local v865 validation completed:

- Built `arpsid_sid808_audible_authority_v865_tests`.
- Built AUv2 bundle target `arpsid_auv2`.
- Built AUv2 and AU3 SID-808 smoke executables.
- Focused CTest SID-808/transport/no-silence suite: 8/8 passed.
- Direct AUv2 component smoke: PASS.
- Direct AUv2 SID-808 component smoke: PASS, including channel 1 and channel
  10 GM drum hits with non-silent output.
- Direct AU3 SID-808 render-event smoke: PASS, including MIDI 1.0 and MIDI 2.0
  UMP event-list hits with non-silent output.
- AUv2 install/cache refresh/strict validation: PASS.
- Codesign verification: PASS.
- Installed AUv2 binary SHA256:
  `8769abd235de63e86bace95a34f5651843f0edb65d385d432582553a2954bdbc`.

## Documentation

- Added `SID808_AUDIBLE_AUTHORITY_CLOSURE_V865.md` with the detailed audit
  closure matrix and telemetry contract.

## Carried Forward

- v862 target-routed drum MIDI remains the input authority fix.
- v861 fail-open SID-808 bridge replacement remains the output safety fix.
- v864 SID-core parity fixes remain current for low-level SID behavior.
