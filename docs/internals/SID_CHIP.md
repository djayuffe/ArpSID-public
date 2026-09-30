# SID chip core — low-level reference

The emulated MOS 6581 / 8580 Sound Interface Device: oscillators, noise,
waveform combination, DACs, envelope generator, filter, output stage and the
analog/forensic terms around them. Every synth engine (BitPerfect, single
SID, SID register, DrSID, SID-808, the DIGI isolated SID) renders through
these classes, so this page is the ground truth for how ArpSID sounds.

| Source | Contents |
|---|---|
| `include/arpsid/core/sid_chip.h` | `SIDVoice`, `SIDFilter`, `SIDChip`, clock constants, hard sync, forensic config, DAC and filter tables, sample planning and interval renderers |
| `include/arpsid/core/sid_envelope_core.h` | `Sid6581Envelope`, the ADSR state machine shared by every engine |
| `include/arpsid/core/sid_filter_core.h` | `FilterCore::` shared filter laws (6581 loading, feedback, leaks, safety clamp) |
| `include/arpsid/core/sid_combined_wave_model.h` | pulse comparator, combined-waveform charge model, bit-weighted ladders |
| `include/arpsid/core/sid_analogue_calibration.h` | per-revision cutoff anchors, resonance Q, DC, gain, external RC |
| `include/arpsid/core/sid_portamento_law.h` | register-domain glide laws |
| `include/arpsid/core/sid_interval_renderable.h`, `sid_chip_interval_native.h` | the sub-cycle lattice and interval-native rendering helpers |
| `include/arpsid/core/sid_measured_chip_variation.h`, `sid_measured_posterior.h`, `sid_variant_profile.h` | measured die profiles, board and output-stage variants |

---

## 1. Clock and constants

| Constant | Value | Meaning |
|---|---|---|
| `PAL_CLOCK_FREQ` | 985 248 Hz | PAL C64 φ2 clock |
| `NTSC_CLOCK_FREQ` | 1 022 727 Hz | NTSC C64 φ2 clock |
| `MAX_SUPPORTED_SID_CLOCK_FREQ` | 10 MHz | upper bound accepted by `sidClockFrequencySupported()` |
| `kSidPhaseMask24` | `0xFFFFFF` | 24-bit oscillator accumulator |
| `kSidHardRestartCycles` | 46 | length of the hard-restart discharge window |
| `kSidHardRestartRegRelatchCycles` | 45 | register relatch delay after a hard restart |
| `kSidHardSyncSourceOf` | `{2, 0, 1}` | voice 1 syncs to 3, 2 to 1, 3 to 2 (the SID sync ring) |
| `kSidSubcycleResolution` | 256 | fractional sub-steps per SID cycle (power of two, asserted) |

Enumerations:

- `Waveform` (the control-register high nibble): `None 0`, `Triangle 1`,
  `Sawtooth 2`, `TriSaw 3`, `Pulse 4`, `TriPulse 5`, `SawPulse 6`,
  `TriSawPulse 7`, `Noise 8`. `kSidWaveformSelectTable` maps the nibble to the
  control-register mask (`index << 4`).
- `FilterMode` (bits 4–6 of `$D418`): `None 0`, `LowPass 1`, `BandPass 2`,
  `LpBp 3`, `HighPass 4`, `Notch 5` (LP+HP), `BpHp 6`, `LpBpHp 7`. Modes are
  a bit mask, so every combination works as on hardware.
- `SIDModel`: `MOS6581 0`, `MOS8580 1`. The revision (2, 3, 4 for 6581;
  5 for 8580) is kept separately; `sidCombinedRevisionForModel()` clamps it
  (6581 → 2..4, 8580 → 5).

---

## 2. Oscillator (`SIDVoice`)

### 2.1 Accumulator

Each voice has a 24-bit phase accumulator `phase`. Per SID cycle
(`stepCycle()` / `SIDChip::stepVoicesOneCycle_()`):

```
inc   = lowFreqMode ? round(frequency * 0.01) : frequency     // 16-bit FREQ register
phase = (phase + inc) & 0xFFFFFF
```

- **LF mode** (the `VCO LF Mode` parameter) divides the increment by 100 so
  the oscillator runs at LFO rates. Both the cycle and sub-cycle paths use the
  same rounding.
- **TEST bit**: holds `phase = 0`, the noise LFSR at `0x7FFFFF`, and clears
  the edge trackers. Releasing TEST resumes from that held state without
  injecting another perturbation. Every TEST assertion bumps
  `testBitCycleStamp_` (used by the combined-wave model).
- Frequency writes only latch the register; they never fabricate extra phase
  or LFSR transitions.

### 2.2 Hard sync and ring modulation

All three voices step from the **same pre-cycle snapshot**, so sync decisions
see one hardware edge epoch rather than a sequentially mutating array.

- `sidPhaseMsbRose(prev, next)`: the accumulator MSB (bit 23) went 0 → 1.
- `sidHardSyncShouldReset(dest, syncEnabled, msbRose)`: resets `dest` when its
  source (`kSidHardSyncSourceOf[dest]`) rose — **unless** the source is itself
  being synced on this same edge (a synced source does not propagate the reset
  around the ring).
- Ring modulation XORs the triangle's MSB with the source voice's MSB:
  `msb = phase[23] ^ (ringMod && sourceMsb)`.

### 2.3 Waveform generators (12-bit)

| Generator | Law |
|---|---|
| Sawtooth | `phase[23:12]` |
| Triangle | `t = phase[22:11]`; if the (ring-modulated) MSB is set, `t ^= 0xFFF` |
| Pulse | `sidPulseComparator12(phase[23:12], PW, is6581, test)` (below) |
| Noise | 8 LFSR taps mapped to DAC bits 11..4; bits 3..0 are always 0 (the dead low nibble of real chips) |

**Pulse comparator** (the single authority shared with the register engine and
the `$D41B` OSC3 readback model):

- `TEST` → `0xFFF` (comparator forced high; the basis of test-bit digis).
- `PW = $000` → constant high on both models.
- `PW = $FFF` → a 1/4096-duty spike train (fires only when the top 12 phase
  bits equal `$FFF`), not constant low.
- 6581 comparator bias: `+1` for `PW ≤ $020`, `+2` for `PW ≥ $F00`; the
  register value itself is never modified.
- Otherwise `top12 >= PW ? 0xFFF : 0x000`.

**Noise LFSR**: 23 bits, feedback `bit22 ^ bit17`, never allowed to reach 0
(reset value `0x7FFFFF`). It is clocked by rising edges of phase bit 19.
`stepNoiseFromPhase()` counts the edges crossed between two phase values
(`delta >> 20`, plus one if bit 19 rose), capped at 32 per step, so large
increments and sub-cycle steps clock it exactly as often as the hardware.
Output taps: bits 22, 20, 16, 13, 11, 7, 4, 2 → DAC bits 11..4.

**TEST output**: with TEST set, pulse-only renders full scale (`0xFFF`);
any combination with tri/saw/noise renders 0 (the tri/saw bus is grounded).

### 2.4 Combined waveforms

When two or more waveforms are selected, `sidAnalogCombined12_Ultra()` models
the analog wired-AND of the waveform bus as a 12-bit charge-sharing network:

1. **Sources** are the selected generators (noise weighted 0.68 on 6581,
   0.52 on 8580).
2. **Initial charge per bit**: `0.72 × bit + 0.28 × local` (a 5-tap
   `1-2-3-2-1` neighbourhood smoothing), averaged over sources, plus a
   **memory leak** from the previous combined output
   (`leakBase = {—, —, 0.048, 0.041, 0.035, 0.018}` by revision, scaled by
   temperature `1 + (T−25)·(0.0092 | 0.0048)` and supply
   `1 + (V−5)·(0.18 | 0.09)`, clamped to 0.24 (6581) / 0.12 (8580)).
3. **12 relaxation passes**:
   `next = selfKeep·c + neighborPull·avg(left,right) + farPull·avg(±2) + driven + impurity`,
   with `selfKeep/neighborPull/farPull/drivenBoost` =
   `0.672/0.241/0.058/1.041` (6581) and `0.821/0.119/0.023/1.009` (8580),
   clamped to `[0, 1.38]`. The impurity is a **deterministic** per-call hash
   (up to 0.022 / 0.009), never a mutable RNG, so offline and live renders
   are identical whatever the buffer split.
4. **Thresholds**: 0.528 (6581) / 0.571 (8580), +0.045 / +0.026 for bits 9–11;
   noise combinations use per-revision threshold tables.
5. **Mix**: the thresholded word is blended with an edge-smeared copy, the
   previous output and a **bit-weighted ladder** (6581 weights
   `2 4 8 15 29 57 114 228 456 912 1826 3644`, 8580 binary): 6581
   `(2m + 2e + p + l)/6`, 8580 `(5m + e + p + l)/8`.
6. **6581 sag**: tri+pulse combinations are capped at `$FC0` (tri+pul) or
   `$FE0` and compressed above `$E00` (`(v−$E00)·5/32` on R2–R3, `·3/32` on R4).

The last result is kept in `lastCombinedWave` (the analog memory) and the
per-voice seed comes from `ArpSIDForensicConfig::chipIdSeed` mixed with the
voice index. When the forensic model is frozen the seed is the fixed
`0xA341316C`.

### 2.5 Waveform DAC

`initTablesOnce()` builds 4096-entry tables once, off the audio thread:

- **8580**: linear, `y = x`.
- **6581**: a bowed NMOS ladder,
  `y = x − 0.105·x(1−x) + (−0.012(1−x) + 0.006x) + 0.018·x(1−x)·sin(2πx)`.

`sidDac12ToBipolar_()` maps the table value to `[-1, 1]` and adds the 6581 DC
term `−0.018 + 0.010·u`. The voice output is `dac × envelope × level`, and
`oscReadByte_ = mix12 >> 4` is what `$D41B` (OSC3) reads back.

---

## 3. Envelope generator (`Sid6581Envelope`)

One canonical ADSR state machine drives every render path.

**State**: `envCounter` (8-bit, the envelope DAC value), `rateCounter`
(15-bit, mask `0x7FFF`), `expoCounter` (exponential divider), `stage`
(`Attack`, `Decay`, `Sustain`, `Release`), the four nibbles, `is6581`,
`adsrDelayHold`, `hardRestartWindowCycles`.

**Rate periods** (φ2 cycles per step, die-measured; rate 15 is 31 251):

| Nibble | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Period | 9 | 32 | 63 | 95 | 149 | 220 | 267 | 313 | 392 | 977 | 1954 | 3126 | 3907 | 11720 | 19532 | 31251 |
| Attack (0→255) | 2 ms | 8 ms | 16 ms | 24 ms | 38 ms | 56 ms | 68 ms | 80 ms | 100 ms | 250 ms | 500 ms | 800 ms | 1 s | 3 s | 5 s | 8 s |

Decay and release take three times the attack figure (the exponential
divider). Sustain clocks at the decay rate.

**Exponential divider** (`kExpoDivByLevel`), latched when the counter reaches
each boundary:

| Level | 94–255 | 55–93 | 27–54 | 15–26 | 7–14 | 0–6 |
|---|---|---|---|---|---|---|
| Divider | 1 | 2 | 4 | 8 | 16 | 30 |

**`tick()` per φ2 cycle**:

1. During a hard-restart window, hold `envCounter = 0`, stage Release, and count
   the window down (46 cycles).
2. Increment `rateCounter`. If `adsrDelayHold` is set (6581), consume one
   cycle and return: this is the **6581 ADSR delay bug**.
3. When `rateCounter` equals the stage's period: reset it and
   - **Attack**: `++envCounter`; on 255 switch straight to Decay on the same
     event.
   - **Decay**: step down through the exponential divider until the sustain
     level (`nibble × 17`), then Sustain.
   - **Sustain**: follow the sustain target; if it moved below the level,
     go back to Decay.
   - **Release**: step down through the exponential divider to 0.
4. `level = envCounter / 255`. The counter drives the envelope DAC directly
   on both models (no bit reversal).

**Gate edges**: `gateOn()` → Attack, `gateOff()` → Release. On the 8580 both
reset `rateCounter` and `expoCounter` (a clean retrigger); on the 6581 (or with
the **6581 ADSR Bug** parameter on) they are preserved and `adsrDelayHold` is
set, reproducing the hardware delay. A rising gate also ends a pending
hard-restart window, so the attack is never clobbered.

**Sustain changes** while running (`onSustainChanged()`) never leave the FSM
stuck: in Decay it jumps to Sustain if the new target is above the level; in
Sustain it goes back to Decay if the target dropped.

**Hard restart** (`performHardRestart()`): zeroes the counters and opens the
46-cycle discharge window. It is a cycle countdown, not a host-time delay, so
render subdivision cannot move it. `SIDVoice::scheduleHardRestart()` drops the
gate, discharges, and re-gates exactly when the countdown ends.

---

## 4. Filter (`SIDFilter`)

A state-variable filter run **once per SID cycle**, with SID-specific
cutoff/Q laws and 6581 non-linearity.

### 4.1 Cutoff law

The 11-bit `FC` register (`$D415–$D416`) is mapped to Hz through 9 calibration
anchors per revision with smooth-step interpolation (`t²(3−2t)`):

| FC | 0 | 64 | 192 | 384 | 768 | 1152 | 1536 | 1856 | 2047 |
|---|---|---|---|---|---|---|---|---|---|
| 6581 R2 (Hz) | 34 | 52 | 102 | 250 | 760 | 1850 | 3900 | 6900 | 9300 |
| 6581 R3 | 30 | 55 | 120 | 320 | 1100 | 2800 | 5400 | 8800 | 11800 |
| 6581 R4 | 28 | 62 | 150 | 420 | 1450 | 3500 | 6500 | 10100 | 13000 |
| 8580 R5 | 18 | 28 | 55 | 180 | 900 | 3100 | 7600 | 13200 | 19800 |

`sidComputeFilterParityLaw()` then applies:

- a model scale (0.92 on 6581, 0.99 on 8580) and the revision calibration
  `kSidFilterCalibrationByRevision` (cutoff × `0.72 / 0.85 / 1.05 / 1.85` for
  R2 / R3 / R4 / R5);
- thermal drift: `×(1 + (0.035 | 0.015)·drift)`;
- supply: `×supplyScale` (0.85–1.15), the product clamped to `[0.90, 1.10]`;
- limits: at least 12 Hz (6581) / 10 Hz (8580), overall `[8, 48000]` Hz.

### 4.2 Resonance law

The 4-bit resonance (`$D417` high nibble) maps to Q:

- 6581: `Q = (0.70 + 0.56n + 4.6n² + 2.4n³) × revBias` with `n = res/15` and
  `revBias` 1.18 (R2), 1.0 (R3), 0.90 (R4);
- 8580: `Q = 0.74 + 0.85n + 8.2n² + 6.8n³`;

then × the revision resonance scale (`1.35 / 1.18 / 0.95 / 0.68`) and clamped
to `[0.25, 7]`.

**Integrator leak** (energy loss of the real op-amp integrators):

- 6581: `0.0030 + 0.0020·(1 − fc/12000) + 0.0008·res`, plus
  `0.018 × distortion × (res−8)/7` above resonance 8;
- 8580: `0.0007 + 0.0005·(1 − fc/20000) + 0.00012·res`;
- plus `(1 − supply)·0.004`, clamped to `[0, 0.05]`.

**How the law is evaluated (performance).** The law is computed in two
stages with identical arithmetic: `sidComputeFilterParityBase()` (anchors,
model scale, revision calibration, Q; it depends on model, `FC`, resonance
and revision only) and `sidFinishFilterParityLaw()` (thermal drift, supply,
limits, integrator leak). `sidComputeFilterParityLaw()` runs both.

- The default analogue calibration (`sidDefaultAnalogueCalibration`: anchors,
  resonance curve, output gain, DC, external RC) is built once per (family,
  revision 0–15) into a static table and read by reference
  (`sidDefaultAnalogueCalibrationRef`); `prewarmAllSidTables()` builds it off
  the audio thread. It used to be rebuilt, sort included, for every sample in
  the filter law and in the chip output stage.
- `SIDFilter::setCutoff` / `setResonance` keep the current law when the value
  is unchanged (the engines re-apply both every render slice); every setter
  of a law input recomputes it, and `reset()` invalidates it.
- The SID register engine's filter derives drift and supply from its input
  level, so they change almost every sample: it caches the base stage per
  (model, `FC`, resonance, revision) and redoes only the finish stage.

Together these halved the VST3 render cost (a held chord: about 46 % → 22 %
of one core at 512-frame blocks) with bit-identical output, checked by
rendering SYNTH, CLASSIC and DR SID scenarios with filter automation through
the old and the new code.

### 4.3 Per-cycle processing (`process()`)

```
smoothedFc += (fc − smoothedFc) · a      a = 1 − exp(−1 / (clock · 0.0015))   // ~1.5 ms
smoothedQ  += (Q  − smoothedQ)  · a
fcEff = 6581 loading(smoothedFc, drive=|in|/2, res)        // FilterCore
g = clamp(2π·fcEff / clock, 1e−7, 0.49),   k = 1 / max(0.2, Q)
x = in − bp · feedback(res)                                 // 6581: 0.040+0.095r, 8580: 0.022+0.135r
x = 6581 nonlinearity(x)                                    // tanh saturation
hp = (x − (k+g)·z1 − z2) / (1 + g(g+k))
bp = (z1 + g·hp) · leak1          lp = (z2 + g·bp) · leak2
out = Σ(selected LP, BP, HP)      clamped to ±8 (safety; counted as a diagnostic)
```

- **6581 op-amp loading** (`FilterCore::effectiveCutoffHz_6581_loading`):
  above drive 0.35 and resonance 0.45 the cutoff is squashed by up to 16 %.
- **6581 saturation** (`applyModelNonlinearity`):
  `tanh(x·(1 + 0.85s)) / (1 + 0.35s)` with `s = (drive−0.35)·res`.
- `leak1 = 1 − leak`, `leak2 = 1 − leak·(0.52 | 0.36)`.
- Denormals are flushed; states are clamped to ±8.
- The same `FilterCore` laws are used by the SID register engine's filter, so
  identical register state sounds identical in both engines.

`snapSmoothingToTarget()` jumps the smoothers to their targets after a cold
reconfigure, so a filtered one-shot on the first block is not muted by the
20 Hz reset placeholder.

---

## 5. Voice mixer and output stage (`SIDChip::renderCurrentState_()`)

Per SID cycle (or sub-cycle step), after the oscillators advance:

1. **Voices**: each voice renders `dac × envelope × level`.
2. **Crosstalk**: each voice picks up `(0.0075 | 0.0035) × crosstalk` of the
   other two.
3. **Envelope TDM skew** (forensic): a charge/discharge model of the
   time-multiplexed envelope DAC skews voices 1 and 3 in opposite directions by
   up to 2 % (6581) / 1 %.
4. **Voice DC**: 6581 `(i−1)·0.0035 + 0.0020·env`, 8580 `0.0004·(i−1)`.
5. **Routing**: voices with their filter bit set (and a filter mode other than
   `None`) go to the filter input, the rest to the dry sum. `3OFF`
   (`$D418` bit 7) mutes voice 3 only on the dry path, as on hardware.
6. **External input** (`EXT IN` bleed): dry `(0.016 | 0.009)`, filtered
   `(0.010 | 0.005)` × bleed.
7. **Filter**, then the **filter ohmic** term (forensic): a load-dependent gain
   reduction up to 16 % (6581) plus a little direct feed.
8. **Sum** `dry + filtered`; the 6581 adds its volume-dependent DC pedestal
   `0.018 + 0.0012·vol`.
9. **Master volume**: `× vol/15` (the `$D418` low nibble, the digi DAC).
10. **`$D418` asymmetry** (forensic), **8580 Digifix** (boosts low volumes up
    to +18 % so volume-register digis are audible on 8580), **system noise**
    (follows voice activity: it fades in with the first sounding voice and
    out, over about 0.1 s, once every envelope has released, so a patch is
    silent between notes), **ADC bleed**, **bus collision**, **POT input** leak.
11. **Revision calibration**: output gain (6581 R2 1.16, R3 1.10, R4 1.05;
    8580 1.00) and DC (28 / 20 / 14 / 3 mV × vol/15).
12. **DC blocker**: first-order high-pass at 16 Hz.
13. **Open bus**: the last written register byte decays bit by bit (every 512
    cycles on 6581, 768 on 8580) and leaks a tiny signal
    (`0.0012 × influence`).
14. **External RC filter** (C64 output stage, optional): low-pass at
    13.2 / 14.2 / 15.2 kHz (6581 R2/R3/R4) or 18.5 kHz (8580), high-pass at
    16 Hz, computed at the SID clock.
15. **Soft clip** `x / (1 + |x|)`.
16. **Motherboard** (forensic): a shelving mix of two low-passes and a
    high-pass.
17. Final clamp to `[-1, 1]`. No limiter runs inside the chip (the
    `LookaheadLimiter` struct is kept for diagnostics but the chip output path
    is latency-free; musical limiting belongs to the output stage).

`readOsc3()` / `readEnv3()` return voice 3's oscillator and envelope bytes for
`$D41B` / `$D41C`. `readOpenBusByte()` returns the decaying open-bus latch.

---

## 6. Sample planning and interval rendering

The chip renders at the SID clock and down-samples to the host rate.

**Planning** (`ensurePlannedSample_()`): for each host sample,
`budget = cycleFrac + clock / sampleRate`; `cycles = floor(budget)`,
`cycleFracEnd = budget − cycles`. The forensic jitter, thermal and ripple terms
**never** change the cycle budget (the host dispatcher owns the φ2 lattice), so
sub-cycle events always land on cycles that exist.

**Renderers**:

| Function | Use |
|---|---|
| `processSample(L, R)` | one host sample: steps every planned cycle and averages them (box filter). With oversampling (2, 4 or 8×) it plans that many sub-samples at the raised rate and combines them with a Hann window. |
| `renderCycleWindowContribution(c0, c1, L, R)` | exactly cycles `[c0, c1)` of the planned sample; used when register writes split a sample at cycle boundaries. |
| `renderSubCyclePhaseContribution(c, s0, s1, L, R)` | sub-steps `[s0, s1)` (of 256) inside cycle `c`: each sub-step advances the phase by its exact share of the increment (`full·(s+1)/256 − full·s/256`), resolves sync on sub-step boundaries, renders the state; envelopes and the noise LFSR advance only at the whole-cycle boundary. |
| `finalizePlannedSample(L, R)` | renders the remaining cycles and normalises by the rendered count. When a sample covers less than one cycle (very high rates), it advances the phase by the fractional cycle instead of holding the last value. |
| `advanceSidCyclesNative(n)`, `advanceSidSubcyclesNative(s0, s1)` | C64/PSID timed-write rendering on the chip's own lattice, bypassing the host-sample planner. |

These give ArpSID **fractional-cycle accuracy**: a register write lands on
its exact cycle, and a note that starts mid-sample starts mid-sample.

---

## 7. Forensic configuration (`ArpSIDForensicConfig`)

Every analog imperfection is a field here; `active(amount)` returns
`amount × intensity` or 0 when disabled or frozen.

| Field | Default | Effect |
|---|---|---|
| `enable`, `intensity` | true, 1.0 | master switch and scale (the plug-in parameter defaults are off) |
| `temperatureCelsius`, `supplyVoltage` | 35 °C, 5.00 V | combined-wave leak, filter supply scale |
| `revision`, `chipIdSeed` | 5, `0xDEADBEEF` | die revision, per-chip combined-wave seed |
| `startupRandomization` | true | random oscillator/LFSR/envelope/filter state at power-on (a "measured" profile keeps envelopes low) |
| `digifix8580` | true | 8580 low-volume boost |
| `clockJitter`, `supplyRipple`, `thermalDrift` (+ enables) | 0.18, 0.20, 0.10 | clock jitter (±2.2e−4), supply ripple (17 Hz / 11 Hz, 2 % / 1.2 %), thermal drift of cutoff and clock |
| `voiceCrosstalk`, `externalBleed` (+ enables) | 1.0 | inter-voice and EXT IN leakage |
| `envelopeTDM`, `d418Asymmetry`, `filterOhmic`, `systemNoise`, `motherboard`, `adcBleed`, `busCollision`, `potInput` | 0 | the output-stage terms in §5 |
| `bitPerfectMode`, `forensicFreeze` | false | freeze all stochastic variation (deterministic output) |
| thermal model | Tj, Tc, Ta 25 °C; RθJC 8.5, RθCA 35 K/W; Cj 0.012, Cc 0.085 J/K; τ 18 s | a two-node junction/case model (`updateThermal()`), clamped to 15–92 °C / 15–68 °C |

`freezeAtPowerOn()` derives a fixed noise seed from temperature and voltage so
frozen renders are reproducible. `SidAnalogReadNoise` produces the upper-nibble
noise of analog register reads, updated every 8 cycles.

---

## 8. Portamento law (`sid_portamento_law.h`)

Glides are **register-domain**: the engine writes intermediate `FREQ` values,
as C64 music routines do.

| Style | Parameter text | Law |
|---|---|---|
| `C64RegisterSlide` | `C64 SLIDE` | `stepUnits = ceil(delta / steps)`, steps spread over the glide time on the **SID-cycle** lattice (`cycleStride = totalCycles / steps`) with Q32 fixed-point timing |
| `C64FixedDelta` | `C64 FIXED` | a fixed `C64 Glide Delta` (1–255) per video frame (50 Hz PAL / 60 Hz NTSC, chosen from the clock) |
| `LinearSemitone` | `LINEAR` | linear in semitones: `12·log2(freq)` interpolated per sample |
| `SmoothSynth` | `SMOOTH` | the same even register steps as `C64 SLIDE`, but timed on the host-sample lattice (`sampleStride`) instead of SID cycles |

`makeDiscreteRegisterGlide()` builds the state; `advanceDiscreteRegisterGlide()`
advances a block in O(1) (it computes how many steps fall in the block and
where the last one lands, as a sample index and a cycle offset, so the write is
scheduled on its exact cycle).

---

## 9. Snapshots and realtime rules

- `SIDVoice::Snapshot` and `SIDChip::Snapshot` capture the complete chip state
  (phases, LFSRs, envelopes, combined-wave memory and seeds, open bus, RC and
  DC filter states, volume, 3OFF, oversampling, revision, model).
  `restore()` sanitizes everything. The C64 rollback journal and state
  persistence use these.
- The DAC and filter tables are built by `prewarmAllSidTables()` during
  plug-in initialisation. `requireSidTablesPrewarmedForRealtime()` and the
  realtime guard (`sid_realtime_guard.h`) forbid a first-time table build on
  the audio thread.
- Every float that leaves a stage passes `ArpSID_sanitizeFloat()` (NaN/Inf →
  0) and denormals are flushed in the filter, DC blocker and RC states.

See also [ENGINES.md](ENGINES.md) for how the engines drive the chip and
[RUNTIME.md](RUNTIME.md) for how register writes are scheduled.
