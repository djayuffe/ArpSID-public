# ArpSID 0.0.690 pass380 v864 - SID Core Audit Closure

Date: 2026-07-04

This pass expands the SID-core documentation and closes the low-level audit
surface from the uploaded v860 SID-core review. Scope is the physical SID core,
C64-facing SID readback, and interval-native timed-write helpers:

- `include/arpsid/core/sid_chip.h`
- `include/arpsid/core/c64_sid_readback.h`
- `include/arpsid/core/sid_chip_interval_native.h`
- `source/tests/multi_sample_rate_render_fingerprint_v529_tests.cpp`
- `source/tests/sid_core_audit_v864_tests.cpp`

## Summary

v864 does not replace the existing SID architecture. The audit confirmed that
the main shape is already correct: three SID voices, a cycle-based ADSR core,
parallel hard-sync resolution, combined-wave modelling, calibrated filter law,
serializable analogue state, PHI2-clocked C64 readback, and subcycle timed-write
interfaces.

The remaining release risk was parity drift between surfaces. A behavior could
be correct in `SIDVoice` but wrong in `SidReadbackModel`, or a helper could
claim native cycle timing while secretly advancing by host samples. v864 fixes
those gaps and adds one high-signal guard that exercises the actual behavior.

During full closure, one additional source of false confidence surfaced:
direct `SIDVoice::setWaveform()` callers were using both internal waveform
nibbles (`0x01` for triangle) and raw SID control-register masks (`$10` for
triangle, `$80` for noise). The core now accepts both forms so raw SID waveform
masks cannot silently select waveform zero.

## Areas Closed

### 1. Pulse-Width Edge Cases

Existing guard coverage remains authoritative:

- `SidCoreExactnessV854Tests`
- `C64PlayBridgeRoutingV855Tests`

Pinned behavior:

- `$000` pulse width is constant high.
- `$800` is exact 50 percent duty in the audio voice path.
- `$FFF` is a 1/4096-duty spike, not constant silence.
- C64 `$D41B` OSC3 readback follows the same `$FFF` spike law.

v864 keeps those laws intact and adds neighboring parity coverage so future
readback/timing changes cannot invalidate them indirectly.

### 2. TEST Bit and Noise Reset

New guard:

- `SidCoreAuditV864Tests`

Pinned behavior:

- Asserting TEST holds oscillator output at zero.
- TEST resets phase and the noise LFSR to the hardware reset state.
- Releasing TEST restarts noise from the same sequence as a fresh reset voice.
- The audio `SIDVoice` path and the C64-facing `SidReadbackModel` path now have
  matching TEST/noise restart semantics.

Why it matters:

C64 tunes and drum/bass patches often use TEST as a deterministic oscillator
reset. If the audio path and `$D41B` readback path disagree, polling code can
make decisions against a state that did not produce the rendered audio.

### 2b. Waveform Select Compatibility

Fixed file:

- `include/arpsid/core/sid_chip.h`

Guard coverage:

- `SidCoreAuditV864Tests`
- `MultiSampleRateRenderFingerprintV529Tests`
- `BitperfectDspAuthenticityV531Tests`
- `ZeroCycleAndParamSmoothingV533Tests`

Pinned behavior:

- `SIDVoice::setWaveform(0x01)` selects triangle.
- `SIDVoice::setWaveform(0x10)` also selects triangle.
- `SIDVoice::setWaveform(0x08)` selects noise.
- `SIDVoice::setWaveform(0x80)` also selects noise.
- Raw SID waveform masks render identically to their internal shifted nibble.

Why it matters:

The direct SID-core API was used by tests and low-level helper paths that read
`0x10` / `0x80` as SID waveform bits. The old implementation masked only the
low nibble, so those calls selected waveform zero and could make rendered-audio
guards measure DC instead of oscillator output.

### 3. Hard-Restart and Gate Timing

New guard:

- `SidCoreAuditV864Tests`

Pinned behavior:

- `scheduleHardRestart()` immediately drops gate.
- The hard-restart window starts at the documented 46-cycle countdown.
- The voice does not re-gate at cycle 45.
- The voice re-gates exactly when the countdown reaches zero on the 46th service
  tick.

Why it matters:

Hard restart is used for tight SID bass, drums, and C64-style retriggering. A
one-cycle-late re-gate softens transients and can make apparently correct
register traces sound wrong.

### 4. Sync and Ring Source Timing

Fixed file:

- `include/arpsid/core/c64_sid_readback.h`

New guard:

- `SidCoreAuditV864Tests`

Pinned behavior:

- The C64 readback model now uses the same no-cascade hard-sync law as
  `SIDChip`.
- In the SID sync ring, a source oscillator that is itself synchronized on the
  same edge does not propagate a second reset through the ring.
- The V1<-V3, V2<-V1, V3<-V2 topology remains stable.

Why it matters:

The audio core already used a parallel snapshot to avoid voice-order sync bugs.
The readback model still used a simpler rule. That meant `$D41B` polling could
see a reset that the audio core did not render. v864 closes that parity drift.

### 5. Filter Routing and Cutoff Law

Existing guard coverage:

- `FilterCoreUnificationAndTopologyModeV532Tests`

New guard coverage:

- `SidCoreAuditV864Tests`

Pinned behavior:

- SID filter modes are combinable bits, not exclusive enum states.
- Notch remains LP+HP.
- LP+BP+HP remains a legal mode.
- 6581 cutoff is monotonic across the 11-bit cutoff register.
- Resonance nibble raises Q.
- 8580 high cutoff remains brighter than 6581 calibration.

Why it matters:

The SID `$D418` filter-mode bits can be combined on real hardware. Treating
notch as a special override or collapsing combined modes loses valid SID sounds,
especially imported C64 register states.

### 6. `$D418` Master-Volume DC Behavior

Fixed file:

- `include/arpsid/core/sid_chip.h`

New guard:

- `SidCoreAuditV864Tests`

Bug fixed:

The calibrated chip DC pedestal was applied after the D418 volume multiply,
after the DC blocker, and after the external RC stage. That meant master volume
0 could still leak a permanent calibrated DC output of roughly 0.02 in a
deterministic 6581 render.

New behavior:

- Revision output gain is still applied to the SID analogue path.
- Calibration DC is moved before the board/DC-blocking stage.
- Calibration DC is scaled by the `$D418` volume DAC.
- Static volume-zero output settles near silence.
- Volume changes can still create D418-style DC transients because the DC lives
  before the blocking stages instead of being deleted entirely.

Why it matters:

The SID volume register is both a master volume control and the classic DIGI
volume-DAC surface. The core must preserve volume-step transients without
turning volume zero into a permanent post-volume DC source.

### 7. Zero-Cycle and Subcycle Timed-Write Rendering

Fixed files:

- `include/arpsid/core/sid_chip.h`
- `include/arpsid/core/sid_chip_interval_native.h`

Existing guard coverage:

- `ZeroCycleAndParamSmoothingV533Tests`

New guard coverage:

- `SidCoreAuditV864Tests`

Bug fixed:

`sidChipAdvanceCyclesNative()` claimed to advance exact SID cycles but called
`processSample()`. At 48 kHz, one host sample represents roughly 20 PAL SID
cycles, so asking for 16 native cycles could advance hundreds of SID cycles.

New behavior:

- `SIDChip::advanceSidCyclesNative()` advances the SID lattice directly.
- `SIDChip::advanceSidSubcyclesNative()` advances exact fractional subcycles on
  the 8-bit subcycle grid.
- `sid_chip_interval_native.h` delegates to those direct native methods.
- `sidChipRenderIntervalNative()` uses direct native subcycle advancement for
  leading/trailing partial cycles instead of the host-planned subcycle helper.
- Native cycle/subcycle helpers are independent of host sample rate.
- Half-open timed-write intervals include the begin boundary and exclude the end
  boundary.

Why it matters:

PSID/RSID timed writes must land at cycle/subcycle positions, not host-sample
positions. If helper names promise native SID time but advance host samples, a
future timed-write renderer can be correct on paper and wrong in audio.

### 8. Readback Parity for C64 Tunes

Fixed file:

- `include/arpsid/core/c64_sid_readback.h`

Guard coverage:

- `C64CycleExactClosureV741Tests`
- `C64SidReadbackPhysicalV711Tests`
- `C64PlayBridgeRoutingV855Tests`
- `SidCoreAuditV864Tests`

Pinned behavior:

- `$D41B` OSC3 is a live oscillator read, not a raw register mirror.
- `$D41C` ENV3 is a live envelope read.
- TEST holds OSC readback at zero.
- Pulse-width `$FFF` readback is a 1/4096 spike.
- Sync no-cascade behavior matches the audio core.
- Noise reset/release behavior matches the audio core.

Why it matters:

C64 tunes can poll OSC3/ENV3 for timing, pseudo-randomness, or control flow.
Readback is not a visual diagnostic; it is part of the emulated machine state.

## Code Changes

- Added `SIDChip::advanceSidCyclesNative(int)` for exact full-cycle native
  advancement.
- Added `SIDChip::advanceSidSubcyclesNative(uint16_t, uint16_t)` for exact
  subcycle advancement on the shared 256-step lattice.
- Changed `sidChipAdvanceCyclesNative()` and `sidChipAdvanceSubphasesNative()`
  to use those native methods instead of `processSample()` or planned-sample
  subphase rendering.
- Changed `sidChipRenderIntervalNative()` whole-cycle rendering to accumulate
  direct native cycle output, and changed partial-cycle rendering to use direct
  native subcycle output.
- Made `SIDVoice::setWaveform()` accept both internal shifted waveform nibbles
  and raw SID control-register waveform masks.
- Moved calibrated analogue DC before the DC blocker and made it volume-owned.
- Added no-cascade hard-sync parity to `SidReadbackModel`.
- Added `SidCoreAuditV864Tests`.

## Validation

Targeted validation completed:

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

Full release validation completed:

- `release-logs/macos-closure-20260704-004703.log`
- release-check curated guards: 7/7 passed
- full CTest: 384/384 passed
- `SidCoreAuditV864Tests` passed in the full suite
- `MultiSampleRateRenderFingerprintV529Tests` passed in the full suite
- strict installed AUv2 verification passed for `ArpS`, `ArIn`, `DrSD`,
  `S808`, and `C64P`
- installed AUv2 binary SHA256:
  `fe08fbbf74ac4db795b77db039f2b20428e3768fc44bffc4e3956cfd37489a72`

## Remaining Boundaries

The SID-core source fixes are closed, guarded, built, installed, and validated
for the local AUv2 macOS release path. External product gates remain separate:

- Apple notarization
- VST3 SDK/toolchain validation
- AUv3 product packaging/runtime validation
