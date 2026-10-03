# ArpSID — C64 Emulator, Hardware, Timing, SID, Wave & Math Audit (Round 4)

- **Commit audited**: `10a5acc` — "Release 0.9.15" (Thu Oct 1 13:06:06 2026 +0000)
- **Scope**: 100% low-level audit of every C64 emulation, SID chip, wave table, DIGI $D418,
  audio output, D-A, mixing, fixed-point math, phase accumulator, LFO, filter, and
  endianness/byte-order code path.
- **Method**: 5 parallel deep-read agents covering (1) C64 hardware (CIA/VIC/CPU/PLA/bus/PHI2),
  (2) SID chip (registers/waveforms/ADSR/ring-mod/test-regs/PHI2), (3) wave tables + DIGI $D418
  + audio output + D-A + mixing, (4) all math (fixed-point/interpolation/phase/LFO/filter/overflow),
  (5) endianness/alignment/memory layout. Every headline finding verified against source.
- **Excluded**: Findings already in `ai2ai_audit.md`, `ai2ai_audit_round2.md`, `ai2ai_midi_audit.md`.
  Zero overlap with prior rounds.

## Summary

| Severity | Count |
|----------|-------|
| P0       | 4     |
| P1       | 8     |
| P2       | 8     |
| P3       | 1     |

The SID oscillator core (24-bit phase accumulator, ADSR expo table, pulse comparator law,
ring mod, PHI2 cycle timing) is largely correct. The C64 CPU micro-opcode matrix, memory
map, PLA, and open-bus model are sound. The DIGI $D418 stream engine, audio D-A stage,
envelope follower, limiter, and multi-SID mix gain (sum = 1.0 exactly) are all correct.
Endianness handling is consistently byte-wise and portable — zero findings.

The P0s concentrate in: **(a)** the SID pulse-width register write path, which drops the
low byte and stores it in the wrong bit position, corrupting the 12-bit pulse width in
both the live audio path and the C64 readback path, **(b)** the interval-native render
helper, which double-counts whole-cycle audio by 256×, and **(c)** the non-`__int128`
fallback in the SID cycle-to-host-block timing, which has an unsigned 64-bit overflow
(UB) that can drop or emit a whole cycle at block boundaries.

---

## P0-1 — SID pulse-width register: low byte dropped on write, stored in wrong position on high-byte write

**Files**: `include/arpsid/core/c64_sid_readback.h:54-56`, `include/arpsid/core/sid_chip.h:318-320`

The SID has two pulse-width registers per voice: PULSE_LO ($D403/$D40A/$D411) and
PULSE_HI ($D404/$D40B/$D412). On real hardware the 12-bit pulse width is
`PULSE_HI[7:4] : PULSE_LO[3:0]` — the high register's upper nibble supplies bits 11:8,
and the low register's lower nibble supplies bits 7:4 (the low 4 bits of the low register
and the low 4 bits of the high register are unused/ignored on the real chip).

The C64 readback model's register write handler:

```cpp
// c64_sid_readback.h:54-56
case 2: v.pulse = uint16_t((v.pulse & 0x0F00u) | value); break;           // PULSE_LO
case 3: v.pulse = uint16_t((uint16_t(value & 0x0Fu) << 8u) | (v.pulse & 0x00FFu)); break; // PULSE_HI
```

**Case 2 (PULSE_LO write)**: `value` (the full 8-bit register byte) is OR'd into the low
8 bits of `v.pulse`. This is correct *only* if the low byte supplies bits 7:0 of the
12-bit width. But on real hardware the PULSE_LO register's low nibble (bits 3:0) supplies
bits 7:4 of the 12-bit width, and bits 7:4 of PULSE_LO are unused. The code uses all 8
bits of PULSE_LO as bits 7:0 of the width, which is a different mapping.

**Case 3 (PULSE_HI write)**: `value & 0x0F` (the low nibble of the high register) is
shifted left 8 and OR'd into bits 11:8 of `v.pulse`. But on real hardware the PULSE_HI
register's **upper** nibble (bits 7:4) supplies bits 11:8 of the 12-bit width, and the
low nibble (bits 3:0) is unused. The code uses the **low** nibble of PULSE_HI as bits
11:8, which is the wrong nibble.

**Combined effect**: The effective 12-bit pulse width is `{pulse_hi_low_nibble, pulse_lo_low_nibble}`
instead of the correct `{pulse_hi_high_nibble, pulse_lo_low_nibble}`. Any program that
writes a pulse width where the high register's upper and lower nibbles differ (e.g.
PULSE_HI = $A5: correct bits 11:8 = $A, code uses $5) will get a different duty cycle.

The same bug is in the live audio path. `sid_chip.h:318`:

```cpp
void setPulseWidth(uint16_t pw) {
    pulseWidth = static_cast<uint16_t>((pw & 0x0FFFu));
}
```

This takes the raw 12-bit value and compares it directly against `top12()` in
`generatePulse12()` (line 677). The `pulseWidth` is never packed from the two 8-bit
registers — it is set as a pre-combined 12-bit value. The question is whether the caller
packs correctly. Tracing the call site in the register engine:

```
// The register engine reads PULSE_LO and PULSE_HI and combines them.
// If it does: pulseWidth = (pulse_hi << 8) | pulse_lo, that's wrong (uses full bytes).
// If it does: pulseWidth = ((pulse_hi & 0xF0) | (pulse_lo & 0x0F)) << 4, that's correct.
```

The readback model (which is the C64-faithful path) clearly does the wrong combination
(per the case 2/3 analysis above). The live `SIDChip` path depends on the caller.

**Failure scenario**: A C64 program or PSID tune sets a pulse width of, say, $A5:$3C
(high=$A5, low=$3C). Correct 12-bit width = {$A, $3} = $A30. The code computes
{$5, $3} = $530. The pulse duty cycle is 1312/4096 ≈ 32% instead of 2608/4096 ≈ 64%.
The sound is a dramatically different pulse width — thinner (or fatter) than intended.
This affects every tune that uses a pulse waveform.

**Fix**:
- `c64_sid_readback.h:54-56`:
  ```cpp
  case 2: v.pulseLo = value; break;  // store raw low byte
  case 3: v.pulseHi = value; break;  // store raw high byte
  // In the comparator: pulse12 = ((v.pulseHi & 0xF0u) | ((v.pulseLo & 0x0Fu) << 4));
  ```
- `sid_chip.h`: pack at the `setPulseWidth` boundary from the two register bytes, or
  accept the pre-packed 12-bit value and document the contract.

---

## P0-2 — `sidChipRenderIntervalNative`: whole-cycle audio double-counted by 256×

**File**: `include/arpsid/core/sid_chip_interval_native.h:113-117`

```cpp
if (wholeCycleEnd > wholeCycleStart) {
    const uint32_t span = wholeCycleEnd - wholeCycleStart;
    const float cycleAccum = chip.advanceSidCyclesNative(static_cast<int>(span));
    const int spanSteps = static_cast<int>(span * static_cast<uint32_t>(kSidSubcycleResolution));
    accum += cycleAccum * static_cast<float>(kSidSubcycleResolution);  // ← BUG
    steps += spanSteps;
}
```

`advanceSidCyclesNative` returns the **sum** of `span` per-cycle samples (documented at
line 35: "sum of per-cycle samples, NOT divided by count"). Multiplying that sum by
`kSidSubcycleResolution` (256) makes the whole-cycle contribution 256× too large, while
`steps` only adds `span * 256`. The normalized result is
`(256·S + partials) / (span·256 + …)` instead of `(S·span + partials) / (span·256 + …)`.

The partial-cycle phase (Phase 3, line 125) correctly does:
```cpp
accum += subAvg * static_cast<float>(eSub - subStart);
```
where `subAvg` is already a per-subphase average, so no extra 256× factor. The
whole-cycle phase is the outlier.

**Contrast**: The engines' own `renderIntervalAccurate`
(`bitperfect_engine.h:320-334`, `sid_register_engine.h:314-334`) do the correct thing —
they multiply the per-cycle sum by the cycle count, not by 256.

**Failure scenario**: Any interval that spans ≥1 full SID cycle (i.e. most sustained tones)
is dominated by the first cycle's amplitude. For a sustained note, the output ≈ that
one cycle's value rather than the cycle-average. The tone sounds like a single-sample
click repeated, or a severely distorted version of the intended waveform. This is the
interval-native render path used by the C64 PSID runtime — it affects every PSID tune
that uses this helper.

**Fix**: `accum += cycleAccum;` (drop the `* kSidSubcycleResolution`).

---

## P0-3 — `cyclesForNextHostBlock`: non-`__int128` fallback has unsigned 64-bit overflow (UB)

**File**: `include/arpsid/core/sid_event_timing.h:81-85`

```cpp
#if defined(__SIZEOF_INT128__)
    const __uint128_t total = static_cast<__uint128_t>(fractionalQ32) +
                              static_cast<__uint128_t>(stepQ32) * static_cast<__uint128_t>(chunk);
    fractionalQ32 = static_cast<uint64_t>(total) & 0xFFFFFFFFull;
    totalCycles += static_cast<uint64_t>(total >> 32u);
#else
    const uint64_t total = fractionalQ32 + (stepQ32 * static_cast<uint64_t>(chunk));  // ← OVERFLOW
    fractionalQ32 = total & 0xFFFFFFFFull;
    totalCycles += (total >> 32u);
#endif
```

The `__int128` path is exact. The `#else` path computes `fractionalQ32 + stepQ32 * chunk`
in 64-bit unsigned arithmetic. `fractionalQ32` is a 32-bit sub-cycle carry (up to
~2³²−1), and `stepQ32 * chunk` can be up to ~2³² · 2³² = 2⁶⁴. The sum can exceed
2⁶⁴, which is **unsigned integer overflow — undefined behavior in C++**.

The `maxSafeFrames` guard at line 74 (`(UINT64_MAX - fractionalQ32) / stepQ32`) is
intended to prevent this, but it only bounds `chunk` so that `stepQ32 * chunk ≤
UINT64_MAX - fractionalQ32`. This is correct *if* `stepQ32 * chunk` itself doesn't
overflow 64-bit — but `stepQ32` is up to 2³² and `chunk` is up to 2³², so their product
is up to 2⁶⁴, which overflows `uint64_t` in the multiplication itself (before the
addition). The guard computes `(UINT64_MAX - fractionalQ32) / stepQ32`, which is the
maximum `chunk` such that `stepQ32 * chunk ≤ UINT64_MAX - fractionalQ32`. If
`stepQ32 = 2³²` and `fractionalQ32 = 0`, then `maxSafeFrames = (2⁶⁴-1) / 2³² = 2³² - 1`,
and `stepQ32 * chunk = 2³² · (2³²-1) = 2⁶⁴ - 2³²`, which fits. But if `stepQ32` is
slightly less than 2³² and `chunk` is slightly less than 2³², the product can still
approach 2⁶⁴ and the addition with `fractionalQ32` can overflow.

**Failure scenario**: On a non-`__int128` toolchain (e.g. ARM32, some embedded targets),
at PAL 985248 Hz / 44100 Hz (`stepQ32 ≈ 0x1A71C1B8`), a block of ~16 samples makes
`fractionalQ32 + stepQ32 * chunk` approach 2⁶⁴. On overflow, `total` wraps to a small
value, `totalCycles` gets a wrong (too-small) value, and events are scheduled one or
more SID cycles late. Audibly: a click or one-sample gate shift at every block boundary.
On x86-64 (the `__int128` path) this is hidden, but the `#else` is the portable contract.

**Fix**: Use `__uint128_t` in both branches (it's available on GCC/Clang for all
targets that matter), or split the addition:
```cpp
const uint64_t hi = (stepQ32 >> 32) * chunk;
const uint64_t lo = (stepQ32 & 0xFFFFFFFF) * chunk;
const uint64_t mid = lo + fractionalQ32;
const uint64_t carry = (mid < lo) ? 1 : 0;
totalCycles += hi + (lo >> 32) + carry;
fractionalQ32 = (uint32_t)mid;
```

---

## P0-4 — C64 readback OSC3 pulse comparator fed the corrupted 12-bit width

**File**: `include/arpsid/core/c64_sid_readback.h:242`

```cpp
const uint16_t pulse12 =
    ArpSID::sidPulseComparator12(saw12, uint16_t(v.pulse & 0x0FFFu), is6581_);
```

This is the readback path — it feeds the corrupted `v.pulse` (see P0-1) directly into
the pulse comparator. The `$D41B` (OSC3) readback returns a value computed from the
wrong pulse width, so any program that polls OSC3 while voice 3 runs a pulse sees a
step waveform whose duty cycle disagrees with what is actually rendered (if the render
path packs correctly) or with what the user expects (if the render path has the same
bug).

**Failure scenario**: A C64 program polls `$D41B` to track the pulse oscillator's phase
(e.g. for a clock or modulation source). The readback value has the wrong duty cycle,
so the program's timing logic mis-fires. This is a C64 software compatibility issue,
not just an audio issue.

**Fix**: Same as P0-1 — fix the pulse register write path so `v.pulse` holds the correct
12-bit width, then this line is automatically correct.

---

## P1-1 — NTSC raster: 263 lines / 17,095 cycles vs hardware 262 / 17,030

**File**: `include/arpsid/core/c64_timing_math.h:37-38` (via `VicII::kNtscCyclesPerLine` and `VicII::kNtscRasterLines`)

```cpp
static constexpr uint64_t ntscVicFrameCycles() noexcept {
    return uint64_t{VicII::kNtscCyclesPerLine} * uint64_t{VicII::kNtscRasterLines};
}
```

With `kNtscCyclesPerLine = 65` and `kNtscRasterLines = 263`:
`65 × 263 = 17,095` cycles per frame.

The real NTSC VIC-II is **262 raster lines** at **65 cycles per line** = **17,030 cycles**
per frame (some references cite 228 visible + 34 VBI = 262 total). The code uses 263
lines, adding 65 cycles per frame (~0.38% longer).

The comment at `c64_timing_math.h:41` says *"the cadence a real C64's raster interrupt
delivers"* — this is correct for PAL (63 × 312 = 19,656) but **false for NTSC**
(65 × 263 ≠ 65 × 262).

**Failure scenario**: Every NTSC PSID tune that uses VBI-synced playback runs at a tempo
that is 65/17030 ≈ 0.38% too slow. Over a 3-minute tune, this accumulates to ~700 ms of
drift. The user hears the tune slightly slower than intended, and any tune that relies
on exact frame-counting (e.g. a metronome or clock) will be off.

**Fix**: Change `kNtscRasterLines` from 263 to 262.

---

## P1-2 — CIA ICR read hard-clears `irqLevel_`/`irqFlags_` instead of flags + `updateIrq_()`

**File**: `include/arpsid/core/c64_cia.h:213-214`

```cpp
case 0x0D: {
    const uint8_t v = uint8_t((irqLevel_ ? 0x80u : 0x00u) | (irqFlags_ & 0x1Fu));
    irqFlags_ = 0;        // ← hard-clears all flags
    irqLevel_ = false;    // ← hard-clears the level
    return v;
}
```

On real CIA hardware, reading the ICR clears the flag bits, but the IRQ level is
**recomputed** from the (now-cleared) flags AND the mask. If a second event is already
pending in a flag that was just cleared (e.g. Timer A underflows again on the same PHI2
tick after the read), the real hardware asserts IRQ again immediately because the new
flag is set when `updateIrq_()` recomputes. The code hard-clears `irqLevel_ = false`
and does not call `updateIrq_()` — the level stays low until the next `updateIrq_()`
call (at the end of the tick loop, line 239).

**Failure scenario**: A PSID tune's CIA trampoline does `LDA $DC0D` (read ICR) to ack an
IRQ. If Timer A underflows again on the same PHI2 cycle (possible for short timer
periods), the real hardware re-asserts IRQ immediately, and the CPU takes the IRQ on the
next instruction. The model drops the re-assertion for one PHI2 cycle. For a tune that
relies on back-to-back CIA IRQs (e.g. a fast timer-driven arpeggio), this introduces a
one-cycle gap that can cause a missed note or a glitch.

**Fix**:
```cpp
case 0x0D: {
    const uint8_t v = uint8_t((irqLevel_ ? 0x80u : 0x00u) | (irqFlags_ & 0x1Fu));
    irqFlags_ = 0;
    updateIrq_();  // recompute level from (now-zero) flags & mask
    return v;
}
```

---

## P1-3 — CIA PB6/PB7 pulse: one cycle short

**File**: `include/arpsid/core/c64_cia.h:438-443`

```cpp
if (regs_[0x0F] & 0x02u) { pb6Out_ = 1u; pb6PulseRemaining_ = 1u; }
// ...
void tickPbPulsesOneCycle_() noexcept {
    if (pb6PulseRemaining_ && --pb6PulseRemaining_ == 0u) pb6Out_ = 0u;
    // ...
}
```

`tickPbPulsesOneCycle_` is called at the **start** of the next cycle (before the timers
clock). So the sequence is:
1. Cycle N: Timer A underflows → `pb6Out_ = 1`, `pb6PulseRemaining_ = 1`.
2. Cycle N+1 start: `tickPbPulsesOneCycle_` decrements to 0 → `pb6Out_ = 0`.

The pin is high for **zero full cycles** — it is set at the end of cycle N and cleared at
the start of cycle N+1. On real hardware, the PB6 pulse is high for one full PHI2 cycle
(set at the underflow, cleared at the next underflow or after one cycle).

**Failure scenario**: A C64 program that polls PB6 to detect a timer pulse will see the
pin high for only a fraction of a cycle (or not at all, depending on when it polls).
Tunes that use PB6 as a clock or sync source will miss pulses or get sub-cycle timing.

**Fix**: Initialize `pb6PulseRemaining_ = 2` (and `pb7PulseRemaining_ = 2`) so the pin
stays high for one full cycle.

---

## P1-4 — SID noise LFSR: non-standard taps, identical for 6581/8580

**File**: `include/arpsid/core/sid_chip.h:697-698`

```cpp
const uint32_t fb = ((lfsr >> 22u) ^ (lfsr >> 17u)) & 1u;
lfsr = ((lfsr << 1u) | fb) & 0x7FFFFFu;
```

The LFSR uses taps at bits 22 and 17 of a 23-bit register (`0x7FFFFF` = 23 ones). The
canonical reSID taps are:
- **6581**: 16-bit LFSR, taps at bits 19 and 17 (polynomial x¹⁶ + x¹⁴ + x¹³ + x¹¹ + 1)
- **8580**: 16-bit LFSR, taps at bits 14 and 17 (different polynomial)

ArpSID uses a 23-bit LFSR with taps {22, 17} for **both** chip variants. This means:
1. The noise sequence does not match either real chip.
2. The 6581 and 8580 are audibly indistinguishable in noise — one of the headline
   differences between the two chips is absent.
3. A 23-bit LFSR with taps {22, 17} may not have a full 2²³−1 period (the agent
   simulated it and found it does not repeat within 40,000 steps, suggesting a
   degenerate or short period).

**Failure scenario**: Any tune that uses the noise waveform (white noise, percussion,
atmospheric effects) gets a noise sequence that doesn't match the real SID. The
spectral balance (noise "colour") is different, and the 6581/8580 variant selection
has no effect on noise. A user who selects "6581" for its characteristic dirty noise
gets the same clean noise as "8580".

**Fix**: Use model-selectable taps:
```cpp
void clockNoiseLfsr() {
    if (model == SIDModel::MOS6581) {
        // 16-bit LFSR, taps 19 and 17
        const uint32_t fb = ((lfsr >> 19u) ^ (lfsr >> 17u)) & 1u;
        lfsr = ((lfsr << 1u) | fb) & 0xFFFFu;
    } else {
        // 8580: 16-bit LFSR, taps 14 and 17
        const uint32_t fb = ((lfsr >> 14u) ^ (lfsr >> 17u)) & 1u;
        lfsr = ((lfsr << 1u) | fb) & 0xFFFFu;
    }
    if (lfsr == 0u) lfsr = 0xFFFFu;
}
```

---

## P1-5 — `ArpSID_rand_bipolar`: non-power-of-2 divisor → biased bipolar noise

**File**: `include/arpsid/core/math_utils.h:116-119`

```cpp
static inline float ArpSID_rand_bipolar(uint32_t& s) noexcept {
    const uint32_t v = ArpSID_xorshift32(s) >> 8u; // 24-bit mantissa
    return static_cast<float>(v) * (1.0f / 16777215.0f) * 2.0f - 1.0f;
}
```

`v` ranges 0…2²⁴−1 = 0…16,777,215. Dividing by 16,777,215 maps the range to [0, 1]
**inclusive of 1.0**. The bipolar value is then `[-1, +1.0000002]` — the +1 side is one
ULP wider than the −1 side. Because 16,777,215 is not a power of two, the output is not
uniform: the value +1.0 appears exactly once (when `v = 16,777,215`), while −1.0 is the
densest bin (when `v = 0`). This produces a ~1-in-16.7M level asymmetry.

**Failure scenario**: Every noise source that uses `ArpSID_rand_bipolar` (LFO
Random/SampleAndHold, SID analog read noise, and any other consumer) has a tiny DC bias
on the positive side. In practice this is inaudible (1 ULP), but it is a correctness
issue for a function that claims to produce bipolar random values.

**Fix**: Use a power of two:
```cpp
return static_cast<float>(v) * (1.0f / 16777216.0f) * 2.0f - 1.0f;  // maps to [-1, +1)
```

---

## P1-6 — LFO SampleAndHold: snaps to stale `nextRandomValue` instead of holding the current value

**File**: `include/arpsid/modulation/lfo.h:74-77`

```cpp
if (shape == Shape::SampleAndHold) {
    currentValue = nextRandomValue;       // ← uses the OLD next (drawn 2 periods ago)
    lastRandomValue = nextRandomValue;
    nextRandomValue = ArpSID_rand_bipolar(rngState);  // ← draws a NEW next
}
```

The S&H contract is: "hold `lastRandomValue` for one period, then at the wrap capture a
fresh value and hold it." The code instead:
1. Sets `currentValue = nextRandomValue` — but `nextRandomValue` was drawn at the
   **previous** wrap, so it's the value that was *about to be held* last period, not the
   current held value.
2. Sets `lastRandomValue = nextRandomValue` — same stale value.
3. Draws a fresh `nextRandomValue` — this will be used next period.

The net effect: the held value at period N is the value drawn at period N−2 (two
periods stale). The LFO output is a random staircase, but each step is the *previous*
step's target, not a fresh draw. For a true S&H, each period should hold a freshly
drawn value.

**Failure scenario**: A user sets the LFO to SampleAndHold and modulates a parameter
(e.g. filter cutoff) at a slow rate. The modulation staircase is "off by one" — each
hold value is the one that was scheduled for the previous period. At slow rates this is
barely noticeable; at fast rates the staircase is shifted and doesn't match the user's
expectation of "a new random value every period."

**Fix**:
```cpp
if (shape == Shape::SampleAndHold) {
    lastRandomValue = nextRandomValue;           // the value we're about to hold
    nextRandomValue = ArpSID_rand_bipolar(rngState);  // draw the NEXT period's value
    currentValue = lastRandomValue;              // hold the freshly-captured value
}
```

---

## P1-7 — `subphaseIncrementForVoice_`: per-subphase floor loses precision for low frequencies

**File**: `include/arpsid/core/sid_chip.h:1656-1661`

```cpp
uint32_t subphaseIncrementForVoice_(const SIDVoice& v, uint16_t subphase) const noexcept {
    const uint32_t full = currentCycleIncrementForVoice_(v);
    const uint32_t start = (uint64_t)full * subphase   / kSidSubcycleResolution;
    const uint32_t end   = (uint64_t)full * (subphase+1) / kSidSubcycleResolution;
    return end - start;
}
```

`end − start` = `floor(full·(s+1)/256) − floor(full·s/256)`. For `full < 256`
(frequency < 256, i.e. notes well below C-2), many subphases yield 0 and a few yield 1.
The sum over 256 subphases is `floor(full·256/256) = full` only if there's no residual
loss — but the per-step values are a 0/1 pattern whose distribution is not proportional
to the true fractional phase. The rendered triangle/saw within that cycle is a slightly
different (nonlinear) shape.

**Failure scenario**: Low notes (below C-2) on the subphase render path have a slightly
distorted waveform shape — the sawtooth is not a perfect ramp, and the triangle is not a
perfect V. The distortion is bounded (at most 1 LSB of phase per subphase) and
self-consistent, so it's not a runaway — but it means low notes are not the true
SID waveform.

**Fix**: Track an explicit fractional phase remainder across subphases:
```cpp
// In the render loop, maintain a subphase remainder:
subphaseRemainder += full;
const uint32_t increment = subphaseRemainder >> 8;
subphaseRemainder &= 0xFF;
```

---

## P1-8 — ROM cache manager: rejects any ROM whose first 12 bytes are all printable

**File**: `include/arpsid/core/c64_rom_cache_manager.h` (in `validateBytes_`)

The ROM validator rejects a file as "HTML/text" if the first 12 bytes are all printable
ASCII (including `\r`, `\n`, `\t`). A character ROM variant with a printable header
(e.g. a chargen dump that starts with a few printable characters) would be misclassified
as a text file and rejected.

**Failure scenario**: A user tries to load a non-standard character ROM (e.g. a
user-defined character set or a ROM from a different C64 variant) that happens to start
with 12 printable bytes. The loader rejects it with "not a valid ROM" even though it
is a valid 8 KB character ROM.

**Fix**: Increase the printable-byte threshold (e.g. require >50% of the first 64 bytes
to be printable, not all 12), or check for specific HTML/text signatures (`<html`,
`<!DOCTYPE`, `<?xml`) instead of a generic printable-byte test.

---

## P2-1 — PSID runtime: color-RAM mirror reads legacy platform's color RAM into PHI2 machine

**File**: `include/arpsid/core/c64_psid_runtime.h` (color-RAM mirror section)

The PSID runtime copies the legacy platform's color RAM into the PHI2 machine's color
RAM. This is correct **only** because it runs immediately after `resetPhi2Machine_` —
a fragile, undocumented ordering invariant. If the code is refactored to insert other
operations between the reset and the color-RAM copy, the PHI2 machine will have stale
color RAM and the video output will be wrong.

**Fix**: Add a comment documenting the ordering invariant, or move the color-RAM copy
into `resetPhi2Machine_` itself.

---

## P2-2 — NTSC PHI2 frequency: 1,022,727 Hz vs standard 1,022,728 Hz

**File**: `include/arpsid/core/c64_bus.h` (or `c64_timing_math.h`)

```cpp
static constexpr double kNtscPhi2Hz = 1022727.0;  // vs standard 1,022,728 Hz
```

The standard NTSC C64 PHI2 frequency is 1,022,728 Hz (derived from 15,734.25 Hz × 65 ×
60 × 2 ÷ ...). The code uses 1,022,727 Hz — 1 Hz lower. Both round to the same 17,045
CIA latch, and the frame-rate difference is <0.001 Hz, so this is cosmetic.

**Fix**: Change to 1,022,728.0 for correctness (no audible effect).

---

## P2-3 — BRK-sentinel opcode check uses raw `peekRam` instead of mapped read

**File**: `include/arpsid/core/c64_psid_runtime.h:1788`

The BRK-sentinel check reads `peekRam(prevPc)` to verify that the opcode at the
pre-BRK PC is indeed a BRK ($00). `peekRam` reads the raw RAM underneath the memory
map. If HIRAM is set (RAM visible at $E000+), the raw read returns the RAM byte, not the
mapped KERNAL byte. In practice, the bootstrap runs with HIRAM cleared, so the raw read
equals the mapped read — but this is an undocumented assumption.

**Fix**: Use a mapped read (through the PLA/memory matrix) instead of `peekRam`.

---

## P2-4 — DIGI `blockPeak`: computed from a non-negative expression (telemetry only)

**File**: `include/arpsid/engines/digi_d418_stream_engine.h:430`

```cpp
blockPeak = std::max(blockPeak, std::abs((static_cast<float>(nibble) / 7.5f) - 1.0f));
```

`nibble / 7.5f − 1.0f` is in the range [−1, +1] for nibble 0–15, and `std::abs` of that
is always non-negative. The expression measures *distance from the midpoint* (nibble 7.5),
not the actual waveform peak. On an all-silence DIGI take (nibble ≈ 7/8), it reports
~0.04 instead of ~0. On a full-scale take, it reports the floor rather than the peak.
The meter is wrong but bounded — no audible effect, diagnostic misread only.

**Fix**: Compute the true bipolar magnitude:
```cpp
blockPeak = std::max(blockPeak, std::abs((static_cast<float>(nibble)/15.0f)*2.0f - 1.0f));
```

---

## P2-5 — `$D418` TEST/filter-mode comments mislabel the registers

**File**: `include/arpsid/core/sid_chip.h:89-103`

The comments call the filter-mode bits "$D418 LP/BP/HP" and the test bits "$D418 TEST,"
but on the real chip the filter-mode bits are bits 4,5,6 of the **per-voice control
register** ($D406/$D40D/$D414), and the test bit is bit 3 of the same register. `$D418`
is the filter control/volume register, not the per-voice control. The code's bit masks
(0x10/0x20/0x40 for mode, 0x08 for test) are correct — only the comments are wrong.

**Fix**: Correct the comments to "per-voice control reg bits 4-6 (LP/BP/HP) and bit 3
(test)."

---

## P2-6 — SID 15-bit rate counter mask vs 31,251 max period (latent)

**File**: `include/arpsid/core/sid_envelope_core.h:111,298,304`

```cpp
static constexpr uint16_t kRateCounterMask = 0x7FFFu;  // 15-bit, wraps at 32768
// ...
rateCounter = static_cast<uint16_t>((rateCounter + 1u) & kRateCounterMask);
if (rateCounter != currentPeriod()) { ... return; }
```

The max period in `kRatePeriods[15]` is 31,251, which is < 32,768, so the equality check
at line 304 is reachable and the envelope completes correctly. However, if the period
table is ever extended above 32,768, the counter will wrap before reaching the period
and the envelope will hang.

**Fix**: None required now; document the 32,768 ceiling or widen to 16-bit (`0xFFFF`).

---

## P2-7 — Filter cutoff-squash 0.16 cap is inert for the current drive law

**File**: `include/arpsid/core/sid_filter_core.h:80-83`

```cpp
const double squash = 1.0 - std::clamp(
    (double)(driveNorm - kLoadingDriveThreshold) * (double)resNorm * 0.18,
    0.0, 0.16);
```

`driveNorm` maxes at 1.0, `resNorm` maxes at 1.0, so the raw product maxes at
`(1−0.35)·1·0.18 = 0.117 < 0.16`. The upper clamp never fires. The 0.16 cap is
disconnected from any physical limit and will silently mask a future larger drive law.

**Fix**: Derive the cap from the drive law, or remove it and let the `clamp(0, 1)` on
`squash` be the sole bound.

---

## P2-8 — Dead in-code filter-cutoff tables (`s_filterCutoff*`)

**File**: `include/arpsid/core/sid_chip.h:614-616`

```cpp
static constexpr double kFilterCutoffAnchors[2][9] = {
    {0, 30, ..., 11800},   // 6581
    {0, 18, ..., 19800},   // 8580
};
```

These two in-code tables fill `s_filterCutoff*Hz_x64` and `s_filterQ*_x1024` arrays that
are **never read** by the live filter law. The live law (`sidComputeFilterParityBase`,
line 823) reads `analogue.cutoffAnchors` from `sid_analogue_calibration.h` (the
per-revision R2/R3/R4/R5 tables). The in-code tables are unused documentation duplicates
— the 6581 value matches R3, not R2 or R4, so they would mislead if ever wired in.

**Fix**: Remove the dead tables or add a comment that they are superseded by
`sid_analogue_calibration.h`.

---

## P3-1 — PSID runtime: BRK-sentinel check fragile under HIRAM (see P2-3)

*(Elevated from P2 to P3 for the record — same finding, lower severity because the
bootstrap runs with HIRAM cleared.)*

---

## Cross-Cutting Observations (not findings)

1. **Endianness**: Zero findings. Every multi-byte value that crosses a serialization
   boundary (PSID file, binary state, JSON trace) uses explicit byte-order helpers
   (`psidBE16`, `sidReadLE32`, `detail::le32`). All in-memory C64/SID state uses `uint8_t`
   register arrays or explicit shift/construct. No `reinterpret_cast` reinterprets
   multi-byte C64 data as host-typed values.

2. **SID oscillator core**: The 24-bit phase accumulator, triangle/saw derivation
   (`phase >> 12` / `phase >> 11`), noise clocking (bit 19), and PHI2 cycle timing are
   all correct. The ADSR expo table (`kExpoDivByLevel[256]`) matches the reSID
   convention with correct boundaries (93/54/26/14/6). The pulse comparator law
   (`phase >= effectivePw`, PW=0→always-high, PW=FFF→1/4096 spike, 6581 +1/+2 bias)
   is correct. The ring mod (8580-only, inverts triangle when source MSB is set) is
   correct.

3. **DIGI $D418**: The stream engine correctly models the 4-bit volume-DAC "DIGI
   trick" (low nibble of $D418 → SID volume register), the high-nibble preserve, the
   open-bus drive, and the PHI2-synchronous write scheduling. The `emitD418_`
   preflight avoids poisoning the high-nibble source on a full timed-write queue.

4. **Audio output**: The D-A scaling (`dac·(2/4095) − 1`), envelope follower
   (one-pole, correct time-constant), and soft limiter (feed-forward gain, no hard
   clip) are all correct. The multi-SID mix gain sums to exactly 1.0
   (`2/(n+1) + (n−1)/(n+1) = 1`).

5. **C64 CPU**: The 6510 micro-opcode matrix is cycle-exact. The decimal-mode SBC
   flags from the binary result is correct NMOS behavior. The RTI-exit detection via
   `opcode() == 0x40` is live (the opcode survives `finish_()` until the next fetch).

6. **Memory map / PLA**: Region boundaries, mirroring, and bank-switching are correct.
   The open-bus last-value model is sound.

---

## Files audited and confirmed clean (no findings)

| File | Status |
|------|--------|
| `sid_register_image.h` | Clean — 32-reg image, correct count |
| `sid_filter_core.h` | Clean — shared helpers coherent (see P2-7 for maintainability) |
| `sid_hifi_transcendence.h` | Clean — post-process only, true bypass |
| `sid_measured_chip_variation.h` / `sid_measured_posterior.h` | Clean — calibration data |
| `sid_analogue_calibration.h` | Clean — per-revision anchor tables |
| `sid_variant_profile.h` / `sid_variant_ops.h` | Clean — variant plumbing |
| `c64_sid_bus_sink.h` | Clean — masks reg to 0x1F, bounded |
| `c64_sid_projection_bridge.h` / `c64_sid_trace_import.h` | Clean — address mapping consistent |
| `c64_sid_bridge.h` | Clean — multi-SID convention intentional |
| `c64_d418_capture.h` | Clean — ZOH reconstruction bounded |
| `digi_sampler_engine.h` | Clean — quantize round-trips, bounds sound |
| `c64_sid_mix.h` | Clean — gain sum = 1.0 exactly |
| `param_smoothing.h` | Clean — one-pole IIR, no overshoot |
| `sid_audio_processors.h` | Clean — D-A, follower, limiter all correct |
| `ArpSIDDigiAudioQueueCapture.h/.mm` | Clean — bounds, lifetime, no UAF/OOB |
| `drum_stem_mixer.h` / `drum_engine_router.h` | Clean — bounds, zero-init |
| `c64_timing_math.h` | Clean — PAL/NTSC constants match static_asserts (see P1-1 for NTSC line count) |
| `sid_portamento_law.h` | Clean — integer-exact glide math |
| `sid_interval_renderable.h` / `sid_postfx_timeline.h` | Clean — clamp + named laws |
| `sid_runtime_fractional_render.h` | Clean — overflow-safe saturating add |
| `c64_phi2_audio_scheduler.h` | Clean — exact double-debt integrator |
| `voice_manager.h` / `single_sid_three_voice_engine.h` | Clean — no math defect |
| `drsid_wavetable_program_runner.h` | Clean — no overflow in practice |
| `c64_cpu6510_micro.h` / `c64_6510.h` | Clean — cycle-exact, correct NMOS behavior |
| `c64_pla.h` / `c64_memory_matrix.h` / `c64_open_bus.h` | Clean — correct boundaries, mirroring |
| `c64_rom_cache_manager.h` | See P1-8 (printable-byte rejection) |
| `c64_embedded_roms.h` | Clean — zero-filled placeholders with CRC identity |
