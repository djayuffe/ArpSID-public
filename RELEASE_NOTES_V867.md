# ArpSID 0.0.690 pass380 v867 Release Notes

Date: 2026-07-04

## Summary

v867 closes two audible correctness regressions that remained after the
SID-808 bridge authority work:

- SID-808 factory snares could sound like pitched pulse tones and could drone
  because factory one-shots carried synth-style sustain.
- SIDPLAY could still sound wrong through the `SidRegisterEngine` backend
  because the register engine kept an old pulse-width `$FFF` edge bug and C64
  SIDPLAY did not consistently enable `$D418` volume-DAC emulation on the path
  that actually renders register writes.

This release fixes both the drum authorship/gate law and the SIDPLAY
register-engine parity surface, then pins them with behavioral tests.

## Fixed - SID-808 Snare And One-Shot Behavior

- Factory SID-808 snares now author noise:
  - Classic, Punch, Hard, and Wide use `$C0` pulse+noise.
  - Lo-Fi uses `$80` noise.
- All factory SID-808 one-shot drum configs now use sustain nibble `0`.
- The factory slot-variation function preserves zero sustain for one-shot drums,
  so slots 125..149 cannot reintroduce held synth-style sustain while varying
  tune, decay, release, pulse width, and level.
- `Sid808Engine` now gates the fixed SID voice off before writing hit
  parameters, then gates on only after frequency, pulse width, waveform,
  ring/sync/filter routing, ADSR, and voice level have been applied.
- Every SID-808 drum family arms a render-time one-shot auto-release window:
  Kick 80 ms, Snare 35 ms, Closed Hat 12 ms, Open Hat 220 ms, Clap 70 ms,
  Cowbell 260 ms, Tom 160 ms, Rim 15 ms.
- SID-808 block rendering chunks at auto-release boundaries. That lets the
  underlying `SingleSidThreeVoiceEngine` process the release transition and
  free voice allocator slots after the tail instead of leaving stale active
  voices behind.

## Fixed - SIDPLAY Register Engine

- `SidRegisterEngine` now uses a shared 12-bit pulse comparator helper for edge
  cases:
  - `$000` remains constant-high.
  - `$800` behaves as the midpoint comparator.
  - `$FFF` now produces the final one-step high spike instead of being forced
    constant-low.
- The 6581 edge-bias path retains the `$FFF` spike instead of clamping it away.
- C64 SIDPLAY register engines enable `$D418` volume-DAC emulation during C64
  player handoff/render, so volume-register digi/percussion writes are audible
  in the same backend that SIDPLAY uses.
- The C64 handoff preserves the primary SID register image while enabling and
  resetting only the D418 DAC state; secondary SID engines are still reset on
  handoff as before.
- Subphase write queue overflow, coalescing, and dropped-oldest counts remain
  exposed through telemetry so dense timed-write pressure is visible.

## Guard Coverage

New tests:

- `Sid808SnareOneShotV867Tests`
  - verifies all SID-808 factory slots 120..149 resolve to snares with noise;
  - verifies all factory one-shot sustain nibbles are zero;
  - renders every factory snare for one second at 48 kHz;
  - requires audible attack RMS;
  - requires the 500 ms to 1000 ms tail to fall below 2 percent of attack RMS;
  - requires transient zero-crossing density high enough to reject the old
    pulse-only snare shape;
  - requires the SID-808 engine to release the voice after the one-shot tail.
- `SidplayRegisterEngineV867Tests`
  - verifies pulse comparator edges for `$000`, `$800`, and `$FFF`;
  - verifies `$D418` volume-DAC output is silent when disabled and audible when
    enabled;
  - verifies the C64 SIDPLAY render path enables D418 DAC emulation;
  - verifies the old `$FFF` constant-low branch is absent from
    `sid_register_engine.h`;
  - verifies subphase write queue pressure increments diagnostic telemetry.

Focused regression set:

- `SidCoreExactnessV854Tests`
- `SidCoreAuditV864Tests`
- `Sid808EngineAndRouterV535Tests`
- `FactorySid808KitsAndWavetableRunnerV536Tests`
- `DigiD418SidVolumeDacV699Tests`
- `DrumBridgeNoSilenceV613Tests`
- `DrumBridgeBehaviorV621Tests`
- `DrumBridgeMixPolicyV623Tests`
- `Sid808HitOverrideV626Tests`
- `KitSid808FactoryVoicePrecedenceV637Tests`
- `Sid808ProjectionSyncContractV642Tests`
- `DrumBridgeRuntimeAuthorityV644Tests`
- `Sid808ParamVoiceParityV663Tests`
- `DigiMidiPadTransportStopV745Tests`
- `Sid808TargetRoutedDrumMidiV862Tests`
- `Sid808AudibleAuthorityV865Tests`
- `Sid808SnareOneShotV867Tests`
- `SidplayRegisterEngineV867Tests`
- `RenderDrumBridgeDeferralV751Tests`
- `Sid808RestorePreloadDrainV803Tests`
- `Sid808RestoreFlavorIntegrationV816Tests`
- `Sid808StrictAuSlotPolicyV861Tests`

Result: 22/22 PASS locally.

## User-Visible Result

SID-808:

- Snare pads and sequenced snare hits should sound like a noisy drum transient,
  not a narrow pulse oscillator.
- Factory kits should stop cleanly after one-shot hits instead of leaving a
  sustained tone behind.
- Repeated hits should retrigger with clean gate edges because the voice is
  fully programmed before GATE rises.
- Voice telemetry and active voice counts should settle after one-shot tails.

SIDPLAY/C64:

- Tunes that rely on extreme pulse widths should no longer lose the `$FFF`
  pulse edge in the register-engine path.
- Tunes that use `$D418` master-volume changes for digi/percussion should have
  audible volume-DAC edges through C64 SIDPLAY register rendering.
- Dense timed-write pressure remains observable through queue telemetry.

## Relationship To Earlier Fixes

- v861 made SID-808 bridge bus replacement fail-open so silent bridge scratch
  cannot erase fallback audio.
- v862 routed canonical DrSID-mode drum MIDI through the kernel target hook so
  SID-808 bridge hits actually reach `drumEngineBridge_`.
- v865 added audible-minimum and bridge-output telemetry for every drum.
- v866 fixed delayed scheduled-hit telemetry for transport/sequencer events.
- v867 fixes the authored snare/one-shot sound and the SIDPLAY register-engine
  backend parity that those earlier bridge fixes did not address.

## Validation

Completed locally:

- New v867 guards: PASS.
- Focused SID808/SID-core/D418/restore CTest set: 22/22 PASS.
- Full CTest: 387/387 PASS.
- AUv2 bundle target `arpsid_auv2`: PASS.
- Direct AUv2 component smoke: PASS.
- Direct AUv2 SID-808 component smoke: PASS.
- Direct AU3 SID-808 render-event smoke: PASS.
- AUv2 install/cache refresh/strict validation:
  `ARPSID_AUV2_HARD_REFRESH=1 ./build.sh --generator Ninja --install-auv2 --clear-au-cache --validate-auv2 --no-tests --parallel $(sysctl -n hw.ncpu)` PASS.
- Installed AUv2 component smoke: PASS.
- Installed AUv2 SID-808 component smoke: PASS.
- Codesign verification: PASS.
- `auval -a` lists `ArIn`, `ArpS`, `C64P`, `DrSD`, and `S808`.
- Installed AUv2 binary SHA256:
  `878f6216738f820d2d752affb886a03861090f8ceb5994d2a4da73d63fef8ccc`.
