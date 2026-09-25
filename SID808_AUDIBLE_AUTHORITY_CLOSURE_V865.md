# ArpSID 0.0.690 pass380 v865 - SID-808 Audible Authority Closure

Date: 2026-07-04

## Purpose

v865 closes the SID-808 bug class where runtime or GUI telemetry can imply
activity while the actual SID-808 bridge path is silent or unproven. v862 fixed
the critical input-authority bug: canonical DrSID-mode drum MIDI now routes
through the kernel target hook and reaches the SID-808 bridge. v861 fixed the
output-authority policy: the bridge cannot overwrite the final bus with silent
scratch. v865 adds the missing proof layer:

- every SID-808 drum must render audibly through the bridge,
- waveform/control-bit conversion cannot erase a valid waveform,
- the bridge publishes routed hit + configured kit + output peak,
- the UI displays the audio authority that actually owns what the user hears.

## Audit Result

The remaining risk was not another single silent-overwrite bug. It was a
split-authority observability problem.

Before v865, the SID-808 bridge could be the intended audio path, while the UI
still displayed generic DrSID drum levels. That meant the user could see drum
activity sourced from canonical/runtime telemetry, not from the bridge that
actually had to produce the SID-808 sound. In addition, SID-808 waveform values
crossed multiple boundaries:

- factory kit tables store SID control-register waveform bits (`0x10`,
  `0x20`, `0x40`, `0x80`);
- `SIDVoice` internally uses a waveform nibble (`1`, `2`, `4`, `8`);
- GUI/test override surfaces can naturally hand over either form.

The old SID-808 boundary code used `waveform & 0xF0`. That is safe for raw SID
control bits, but it converts valid internal nibbles such as `0x08` (noise) to
zero. Noise-based SID-808 drums are especially exposed to this class: hats,
clap, and snare layers can become waveform-zero even though the UI and MIDI
paths are otherwise healthy.

## Fix 1 - Audible-Minimum Regression For Every Drum

New guard:

- `Sid808AudibleAuthorityV865Tests`

The test renders all eight canonical SID-808 drum families through
`DrumEngineHostBridge`, not just directly through `Sid808Engine`:

- Kick
- Snare
- ClosedHat
- OpenHat
- Clap
- Cowbell
- Tom
- Rim

Each hit must:

- route through active `SID808_AnalogProjection` context,
- render finite left/right samples,
- exceed the audible peak floor,
- exceed the RMS floor,
- increment the SID-808 routed-hit counter,
- retain configured kit slot 120,
- publish the last routed MIDI note and drum class,
- publish nonzero bridge output peak.

This deliberately tests the host bridge because the real Logic/AU bug happened
at the bridge/authority boundary, not inside an isolated voice oscillator.

## Fix 2 - Waveform/Control-Bit Conversion Guard

New helper contract in `include/arpsid/engines/sid808_engine.h`:

- `sid808NormalizeWaveformControl(uint8_t)`
- `sid808ControlWaveformToInternal(uint8_t)`
- `sid808WaveformHasRenderableBits(uint8_t)`

Accepted forms:

| Meaning | Internal nibble | SID control mask |
|---|---:|---:|
| Triangle | `0x01` | `0x10` |
| Sawtooth | `0x02` | `0x20` |
| Pulse | `0x04` | `0x40` |
| Noise | `0x08` | `0x80` |
| All waveforms | `0x0F` | `0xF0` |

The normalizer is applied at every SID-808 boundary that can receive kit or
override waveform values:

- `Sid808Engine::setDrumVoiceConfig()`
- `Sid808Engine::noteOnWithOverride()`
- `Sid808Engine::noteOnWithConfig_()`
- `kitVoiceConfigToSid808()`
- `kitVoiceConfigFromSid808()`

If a positive-level SID-808 hit still arrives with no renderable waveform after
normalization, the render path falls back to that drum's default waveform. This
prevents malformed kit data from creating a silent drum while preserving
`voiceLevel` as the intentional mute control.

## Fix 3 - Bridge/Output Telemetry

New bridge telemetry struct:

- `Sid808BridgeOutputTelemetry`

Published fields:

- `routedHitCount`
- `configuredKitSlot`
- `lastRoutedDrumClass`
- `lastRoutedMidiNote`
- `lastRoutedVelocity`
- `outputPeak`
- `sid808ContextActive`

The bridge publishes this from the same surface that hosts, pads, transport,
and scheduled sample-accurate events use:

- `noteOn()`
- `noteOnWithOverride()`
- `noteOnAt()`
- `noteOnAtWithOverride()`
- `processBlock()`

v866 addendum: the original v865 implementation published routed-hit telemetry
for immediate hits and for `noteOnAt(offset <= 0)`, but delayed scheduled
events with `offset > 0` rendered through a direct router call inside
`processBlock()`. v866 routes those delayed events back through
`noteOn()` / `noteOnWithOverride()` and adds behavioral coverage for delayed,
override, multi-hit, block-end, and overflow scheduled cases. See
`SID808_SCHEDULED_TELEMETRY_CLOSURE_V866.md`.

The AU kernel then copies this into first-class telemetry:

- `sid808ConfiguredKit`
- `sid808RoutedHitCount`
- `sid808LastRoutedDrumClass`
- `sid808LastRoutedMidiNote`
- `sid808LastRoutedVelocity`
- `sid808OutputPeak`
- `sid808BridgeContextActive`
- `sid808BridgeReplacedOutput`

The render path also publishes post-DC bridge peak and whether the SID-808
bridge replaced the final output bus. This distinguishes three important
states:

- routed and audible: bridge has output peak and owns/replaces the bus,
- routed but fail-open: bridge context is active but silent scratch did not
  erase fallback output,
- idle: no bridge context or no configured SID-808 kit.

## Fix 4 - Unified Audible-Authority Display

New AU view helper:

- `ArpSIDUnifiedAudibleAuthorityLabel(...)`

SID-808 authority display includes:

- `AUTH SID808-BRIDGE`
- configured kit slot (`KIT120`, etc.)
- routed hit count
- measured SID-808 bridge peak
- replacement state (`REPL`, `FAILOPEN`, or `IDLE`)

The label is used in:

- realtime presentation HUD,
- drum level line,
- drum HUD line,
- DrSID/SID-808 panel HUD line.

The visible drum peak now includes `tel.sid808OutputPeak`, so these UI elements
follow the bridge's real output:

- drum bus meter,
- drum output dB label,
- drum scope activity blend,
- panel scope activity blend,
- drum pad/meter glow,
- RT LEDs.

## Why This Prevents "Telemetry Moves But No Sound"

v862 ensures a SID-808 hit enters the bridge.
v861 ensures an idle/silent bridge cannot erase the output bus.
v865 ensures the product can prove the bridge really got the hit and produced
output, and that the user-facing HUD names that authority.

The failure mode now has explicit counters:

- no routed-hit count means MIDI/pad/transport did not reach the SID-808 bridge,
- no configured kit means the bridge does not have a playable SID-808 kit,
- routed hits with zero output peak means the bridge rendered silence,
- `FAILOPEN` means silent bridge scratch was not allowed to wipe fallback audio,
- `REPL` means the SID-808 bridge had renderable activity and owned the final
  output bus.

## Validation

Commands run successfully in this source tree:

```sh
cmake --build build --target arpsid_sid808_audible_authority_v865_tests --parallel $(sysctl -n hw.ncpu)
ctest --test-dir build -R Sid808AudibleAuthorityV865Tests --output-on-failure

cmake --build build --target \
  arpsid_auv2 \
  arpsid_auv2_component_smoke \
  arpsid_auv2_sid808_component_smoke_v862 \
  arpsid_sid808_target_routed_drum_midi_v862_tests \
  arpsid_drum_bridge_no_silence_v613_tests \
  arpsid_digi_midi_pad_transport_stop_v745_tests \
  --parallel $(sysctl -n hw.ncpu)

ctest --test-dir build -R 'Sid808AudibleAuthorityV865Tests|Sid808TargetRoutedDrumMidiV862Tests|DrumBridgeNoSilenceV613Tests|DigiMidiPadTransportStopV745Tests|Sid808EngineAndRouterV535Tests|Sid808HitOverrideV626Tests|KitSid808FactoryVoicePrecedenceV637Tests|Sid808StrictAuSlotPolicyV861Tests' --output-on-failure

./build/arpsid_auv2_component_smoke
./build/arpsid_auv2_sid808_component_smoke_v862
./build/au3_sid808_render_event_smoke_v862

./build.sh --generator Ninja --install-auv2 --clear-au-cache --validate-auv2 --no-tests --parallel $(sysctl -n hw.ncpu)
```

Results:

- `Sid808AudibleAuthorityV865Tests`: PASS.
- Focused SID-808/transport/no-silence CTest set: 8/8 PASS.
- AUv2 bundle build: PASS.
- AUv2 component smoke: PASS.
- AUv2 SID-808 component smoke: PASS.
- AU3 SID-808 render-event smoke: PASS.
- AUv2 install/cache refresh/strict validation: PASS.
- Codesign verification: PASS.
- Installed AUv2 binary SHA256:
  `8769abd235de63e86bace95a34f5651843f0edb65d385d432582553a2954bdbc`.
