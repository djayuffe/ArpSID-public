# ArpSID 0.0.690 pass380 v866 Release Notes

Date: 2026-07-04

## Summary

v866 closes the delayed SID-808 scheduled-hit telemetry gap found in the v865
audible-authority audit. The audio path already rendered sample-accurate
scheduled hits, but the delayed dispatch inside `DrumEngineHostBridge` bypassed
the bridge telemetry publisher. That meant a sequencer hit could be audible
while `routedHitCount`, `lastRoutedDrumClass`, and `lastRoutedMidiNote` stayed
stale.

## Fixed

- Delayed `noteOnAt(offset > 0)` events now dispatch through
  `DrumEngineHostBridge::noteOn(...)` instead of calling
  `DrumEngineRouter::noteOn(...)` directly.
- Delayed `noteOnAtWithOverride(offset > 0)` events now dispatch through
  `DrumEngineHostBridge::noteOnWithOverride(...)` instead of bypassing the
  bridge surface.
- Scheduled SID-808 hits now update the same audible-authority telemetry as
  immediate pad/MIDI hits:
  - `routedHitCount`
  - `lastRoutedDrumClass`
  - `lastRoutedMidiNote`
  - `lastRoutedVelocity`
  - active SID-808 bridge context
  - post-render bridge `outputPeak`
- The scheduled ingress comment now states the real contract: it is a
  render-thread scheduled queue, deterministic and fixed-capacity, but not a
  cross-thread GUI/host producer queue.

## Guard Coverage

`Sid808AudibleAuthorityV865Tests` now includes behavioral scheduled-path
coverage in addition to the v865 audible-minimum checks:

- delayed Kick at sample offset 64 renders audibly and increments routed-hit
  telemetry;
- delayed `noteOnAtWithOverride()` renders audibly and reports the final routed
  drum/note;
- multiple scheduled notes in one block increment the hit counter once per
  event and leave the last event in telemetry;
- an event scheduled exactly at the block end publishes routed-hit telemetry in
  that block and renders audibly in the next block;
- scheduled-note queue overflow increments `scheduledNoteOverflowCount()`.

## Validation

Local v866 validation completed:

- Built `arpsid_sid808_audible_authority_v865_tests`.
- `Sid808AudibleAuthorityV865Tests`: PASS.
- Focused SID-808/transport/no-silence CTest set: 8/8 PASS.
- AUv2 bundle target `arpsid_auv2`: PASS.
- Direct AUv2 component smoke: PASS.
- Direct AUv2 SID-808 component smoke: PASS, including channel 1 and channel
  10 GM drum hits with non-silent output.
- Direct AU3 SID-808 render-event smoke: PASS, including MIDI 1.0 and MIDI 2.0
  UMP event-list hits with non-silent output.
- AUv2 install/cache refresh/strict validation:
  `ARPSID_AUV2_HARD_REFRESH=1 ./build.sh --generator Ninja --install-auv2 --clear-au-cache --validate-auv2 --no-tests --parallel $(sysctl -n hw.ncpu)` PASS.
- Codesign verification: PASS.
- Installed AUv2 binary SHA256:
  `f55909a0dfe07b32816a97e2ea2d7fe2c9b5fc4d7be7b87aa9f1924a9d142744`.

## Why This Matters

The v865 UI now exposes a single audible-authority display: hit route, kit,
bridge peak, and replace/fail-open state. Without this v866 fix, transport or
sequencer-driven SID-808 hits could still make sound while the routed-hit
counter appeared frozen. That did not mute the plug-in, but it undermined the
authority display used to debug the original "telemetry moves but no audio"
class of failures.

v866 makes the bridge authority law consistent:

```text
pad hit
MIDI hit
transport hit
scheduled sample-accurate hit
override scheduled hit
        -> DrumEngineHostBridge note surface
        -> SID-808 bridge/router
        -> routed-hit telemetry
        -> bridge output peak telemetry
        -> AU snapshot / AUTH SID808-BRIDGE display
```

## Carried Forward

- v865 audible-minimum coverage for every SID-808 drum remains current.
- v865 waveform/control-bit conversion guards remain current.
- v862 target-routed canonical drum MIDI remains the input authority fix.
- v861 fail-open SID-808 bridge replacement remains the output safety fix.
- v864 SID-core parity fixes remain current for low-level SID behavior.
