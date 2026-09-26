// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// drsid_instrument_program.h — DrSID register-microprogram contract
// (Audit DrSID §3 / §4B).
//
// PURPOSE
// ------// A DrSID drum hit is NOT a generic ADSR-shaped synth voice — it is a tiny
// sequence of timed SID register writes. The audit identified that the
// 0.0.443 DrSID engine was treating drums as "parametric envelopes on top of
// an analog overlay," which is fundamentally wrong for authentic C64
// wavetable-drum behavior (Hubbard/Galway-era drums abuse waveform changes,
// frequency drops, noise bursts, hard-gate restarts, pulse-width movement,
// ADSR re-arming, filter-route changes and the volume DAC).
//
// This header defines the *contract* for a DrSID instrument program: a
// fixed-capacity, render-thread-safe POD format that the upcoming
// `DrSidKitCompiler` (next slice) emits and the upcoming
// `DrSidRegisterProgramRunner` consumes. The format itself is finalized
// here so factories, tests, GUI editors, and serializers all agree on the
// byte layout from day one.
//
// SCOPE OF THIS SLICE
// ------------------// • Define `DrSidRegisterStep`, `DrSidInstrumentProgram`,
// `DrSidControlMap`, `DrSidExpectedFingerprint`.
// • Define `ChokeGroup` and `DrSidVoicePolicy` enums.
// • Pin compile-time invariants (layout, size, monotonic cycle order,
// bounded step count) via `static_assert`.
// • Provide constexpr predicates: `registerStepIsValid()`,
// `programIsWellFormed()` — these are what the compiler will call when
// it ingests author-side kit definitions.
// • Provide constexpr factory builders for the complete 8-class canonical
// DrSID kit so the runtime test can pin the format end to end before any
// larger authored bank is loaded.
//
// WHAT IS DELIBERATELY OUT OF SCOPE
// --------------------------------// • Wiring this format into `DrSidEngine` — that requires the runner
// and is the next slice.
// • Compiling 8+ real factory kits — the audit lists the kit roster
// (Classic 6581 / 8580, Hubbard-ish, Galway-ish, DemoScene, Electro,
// Bass Heavy, Minimal). Those are author-side data, not contract.
// • Choke-group voice allocator. The choke enum lives here because
// every program declares its group; the allocator is a follow-on.
// • Audio fingerprint *capture* — the field is reserved, computation
// happens in the runner.

#ifndef ARPSID_CORE_DRSID_INSTRUMENT_PROGRAM_H
#define ARPSID_CORE_DRSID_INSTRUMENT_PROGRAM_H

#include "arpsid/core/sid_gm_drum_kit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace Drsid {

// ─── Format version pin ─────────────────────────────────────────────────────
// Bump on any incompatible struct-layout change. The serializer / kit cache
// validates this against the stored value and refuses to load mismatches.
inline constexpr std::uint32_t kProgramSchemaVersion = 1u;

// ─── Choke group ────────────────────────────────────────────────────────────
// Hard rule (audit §4B): closed hat MUST choke open hat. Crash MAY choke
// open hat. Clap/snare may share noise voice depending on kit.
enum class ChokeGroup : std::uint8_t {
    None         = 0,
    HiHat        = 1,  ///< closed-hat → chokes open-hat
    Cymbal       = 2,  ///< crash → may choke open-hat
    TomShared    = 3,  ///< toms share kill priority
    NoiseShared  = 4,  ///< clap/snare/hat may share the SID noise oscillator
};

// ─── Voice policy ───────────────────────────────────────────────────────────
// Maps a drum program to a SID voice (or stealing rule). Real C64 has 3
// voices; DualSID has 6. The audit (§4C) demands explicit per-program
// declaration so the allocator is deterministic.
enum class VoicePolicy : std::uint8_t {
    FixedVoice0          = 0,
    FixedVoice1          = 1,
    FixedVoice2          = 2,
    AnyFree              = 3,
    StealOldest          = 4,
    StealQuietest        = 5,
    ChokeGroup           = 6,
    OverlayTableOnVoice  = 7,
};

// ─── Step waveform bit mask ─────────────────────────────────────────────────
// Mirrors the SID $D4x4 control register layout. Bit 0 is GATE (re-arming
// ADSR), bits 4..7 are the four waveform selectors. Authentic C64 drums
// frequently switch waveforms inside a single hit — the format MUST allow
// any combination of these bits.
namespace WaveformBits {
inline constexpr std::uint8_t kGate      = 0x01;
inline constexpr std::uint8_t kSync      = 0x02;
inline constexpr std::uint8_t kRingMod   = 0x04;
inline constexpr std::uint8_t kTest      = 0x08;
inline constexpr std::uint8_t kTriangle  = 0x10;
inline constexpr std::uint8_t kSawtooth  = 0x20;
inline constexpr std::uint8_t kPulse     = 0x40;
inline constexpr std::uint8_t kNoise     = 0x80;
inline constexpr std::uint8_t kWaveMask  = 0xF0;
} // namespace WaveformBits

// ─── Step-level flags ───────────────────────────────────────────────────────
// Orthogonal to the SID register bits — these are runtime directives that
// the program runner interprets per step.
namespace StepFlag {
inline constexpr std::uint8_t kHardRestartTransient = 0x01; ///< pre-pulse $D40B etc. for C64 punch
inline constexpr std::uint8_t kVolumeDacClick       = 0x02; ///< drive $D418 master volume for click body
inline constexpr std::uint8_t kFilterRouteChange    = 0x04; ///< $D417 route bit transitions live
inline constexpr std::uint8_t kADSRReArm            = 0x08; ///< reset gate to force envelope retrigger
inline constexpr std::uint8_t kIsTerminalStep       = 0x80; ///< last step in the program (mirror of stepCount-1)
} // namespace StepFlag

// ─── Register step ──────────────────────────────────────────────────────────
// One row of the DrSID program. Layout chosen to be exactly 16 bytes after
// natural alignment so 32 steps fit in a single 512-byte payload.
struct DrSidRegisterStep {
    std::uint16_t cycleOffset      = 0;     ///< SID cycles since program start; monotonic across steps
    std::uint16_t durationCycles   = 0;     ///< how long this row holds before the next is applied
    std::uint16_t freq             = 0;     ///< $D400/$D401, $D407/$D408, $D40E/$D40F (voice-relative)
    std::uint16_t pulseWidth       = 0;     ///< $D402/$D403 etc. (0..4095 — top 4 bits ignored)
    std::uint8_t  waveform         = 0;     ///< SID $D404 control register (waveform + gate + sync + ring)
    std::uint8_t  attackDecay      = 0;     ///< $D405 nibble layout (attack hi, decay lo)
    std::uint8_t  sustainRelease   = 0;     ///< $D406 nibble layout (sustain hi, release lo)
    std::uint8_t  filterCutoffLo   = 0;     ///< $D415 (low 3 bits, but stored full byte for forward compat)
    std::uint8_t  filterCutoffHi   = 0;     ///< $D416
    std::uint8_t  filterResRoute   = 0;     ///< $D417 (resonance hi nibble, route lo nibble)
    std::uint8_t  modeVolume       = 0;     ///< $D418 (filter mode hi nibble, master volume lo nibble)
    std::uint8_t  flags            = 0;     ///< StepFlag bitfield
};

static_assert(std::is_trivially_copyable<DrSidRegisterStep>::value,
              "DrSidRegisterStep must be trivially copyable for memcpy-based kit cache and atomic snapshot handoff");
static_assert(sizeof(DrSidRegisterStep) == 16,
              "DrSidRegisterStep is pinned at exactly 16 bytes — bump kProgramSchemaVersion if you need to change this");

// ─── Step-level validator (constexpr) ───────────────────────────────────────
constexpr bool registerStepIsValid(const DrSidRegisterStep& s) noexcept {
    // pulseWidth is 12-bit in real SID hardware; we tolerate 0..4095 only.
    if (s.pulseWidth > 0x0FFFu) return false;
    // Filter cutoff lo is technically 3-bit on $D415 — but we store the full
    // byte for forward compat with extended-precision authoring tools. Just
    // ensure resRoute/modeVolume don't carry obvious garbage in the unused
    // high bits of their semantic fields.
    // (No predicate here — every value 0..255 is legal in those bytes.)
    return true;
}

// ─── Maximum program length ─────────────────────────────────────────────────
// 32 steps gives enough room for the longest authentic C64 wavetable hits
// (cymbal-style noise tails, complex toms with waveform sweeps). Real
// programs are typically 4..12 steps. The runner must reject programs whose
// stepCount > kMaxSteps at compile time via `programIsWellFormed`.
inline constexpr std::uint8_t kMaxSteps = 32;

// ─── Instrument program ─────────────────────────────────────────────────────
// One drum hit, fully self-describing. Embedded inline (no heap pointers)
// so the kit registry can hand out programs to the runner via const-pointer
// without lifetime concerns.
struct DrSidControlMap {
    // Maps each high-level GUI control (e.g. "Kick.Tune") to a (step, field)
    // pair so the per-kit knob can rewrite the right register byte at
    // compile time. A real implementation will reference a typed enum for
    // the field; here we expose the raw byte offset within DrSidRegisterStep
    // so the compiler can codegen the rewrite without dispatch.
    //
    // 0xFFu sentinels mean "this control is not bound for this program".
    std::uint8_t tuneStepIndex     = 0xFFu;
    std::uint8_t tuneFieldOffset   = 0xFFu;
    std::uint8_t decayStepIndex    = 0xFFu;
    std::uint8_t decayFieldOffset  = 0xFFu;
    std::uint8_t accentStepIndex   = 0xFFu;
    std::uint8_t accentFieldOffset = 0xFFu;
    std::uint8_t colorStepIndex    = 0xFFu;  ///< "tone"/"hat metal"/"clap spread"
    std::uint8_t colorFieldOffset  = 0xFFu;
};
static_assert(std::is_trivially_copyable<DrSidControlMap>::value, "DrSidControlMap must be trivially copyable");
static_assert(sizeof(DrSidControlMap) == 8, "DrSidControlMap layout pinned at 8 bytes");

struct DrSidExpectedFingerprint {
    // Reserved for register-trace fingerprint pinning. Populated by the
    // runner the first time a program is exercised in CI; future runs
    // compare against this signature to catch silent regressions.
    std::uint32_t registerTraceHash = 0u; ///< CRC32 over (cycleOffset, regIndex, value) triples
    std::uint32_t expectedStepCount = 0u; ///< redundant pin to catch authoring drift
    std::uint64_t firstStepWord     = 0u; ///< low-cost spot-check of the first emitted register frame
};
static_assert(std::is_trivially_copyable<DrSidExpectedFingerprint>::value,
              "DrSidExpectedFingerprint must be trivially copyable");
static_assert(sizeof(DrSidExpectedFingerprint) == 16,
              "DrSidExpectedFingerprint layout pinned at 16 bytes");

struct DrSidInstrumentProgram {
    SidGMDrumClass            drumClass    = SidGMDrumClass::Unsupported;
    ChokeGroup                chokeGroup   = ChokeGroup::None;
    VoicePolicy               voicePolicy  = VoicePolicy::AnyFree;
    std::uint8_t              stepCount    = 0;
    std::array<DrSidRegisterStep, kMaxSteps> steps{};
    DrSidControlMap           controls{};
    DrSidExpectedFingerprint  fingerprint{};
    std::uint32_t             schemaVersion = kProgramSchemaVersion;

    constexpr bool empty() const noexcept { return stepCount == 0; }
};

// Layout pins — these are the contract the serializer and the kit cache rely on.
static_assert(std::is_trivially_copyable<DrSidInstrumentProgram>::value,
              "DrSidInstrumentProgram must be trivially copyable so it can live in pre-allocated kit cache slots");
// Size sanity check (not pinned exactly because trailing padding is
// implementation-defined; we just guard against accidental bloat).
static_assert(sizeof(DrSidInstrumentProgram) <=
                  sizeof(DrSidRegisterStep) * kMaxSteps + 64,
              "DrSidInstrumentProgram size must stay within steps + 64 bytes of metadata");

// ─── Program-level validator (constexpr) ────────────────────────────────────
// Performs every audit-required well-formedness check at compile time:
// 1. stepCount within [1, kMaxSteps]
// 2. cycleOffset is monotonically non-decreasing across [0, stepCount)
// 3. Each step is valid via registerStepIsValid()
// 4. The terminal step carries the kIsTerminalStep flag (sanity pin)
// 5. drumClass is not Unsupported (authored programs target a real drum)
// 6. schemaVersion matches the build's kProgramSchemaVersion
constexpr bool programIsWellFormed(const DrSidInstrumentProgram& p) noexcept {
    if (p.stepCount == 0 || p.stepCount > kMaxSteps) return false;
    if (p.drumClass == SidGMDrumClass::Unsupported)  return false;
    if (p.schemaVersion != kProgramSchemaVersion)    return false;

    std::uint16_t lastCycle = 0;
    for (std::uint8_t i = 0; i < p.stepCount; ++i) {
        const DrSidRegisterStep& s = p.steps[i];
        if (!registerStepIsValid(s)) return false;
        if (i == 0) {
            lastCycle = s.cycleOffset;
        } else {
            if (s.cycleOffset < lastCycle) return false;
            lastCycle = s.cycleOffset;
        }
        const bool isLast = (i + 1 == p.stepCount);
        const bool hasTerminal = (s.flags & StepFlag::kIsTerminalStep) != 0;
        if (isLast != hasTerminal) return false;
    }
    return true;
}

// ─── Constexpr factories — canonical 8-class DrSID kit ──────────────────────
// These compact canonical microprograms cover every DrSID GM drum class used by
// the engine and KIT GUI: Kick, Snare, ClosedHat, OpenHat, Clap, Cowbell, Tom
// and Rim. They are deliberately small, but complete deterministic starter programs: the
// kit compiler can build a complete default DrSID register-program kit from
// this header alone.
inline constexpr std::uint8_t kCanonicalDrSidProgramCount = 8u;

// Kick: pitch drop from F4 (~349 Hz) to E2 (~82 Hz) over ~20 ms, triangle wave.
// Hard-restart transient on step 0 for the C64 punch.
constexpr DrSidInstrumentProgram makeExemplarKick() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::Kick;
    p.chokeGroup   = ChokeGroup::None;
    p.voicePolicy  = VoicePolicy::FixedVoice0;
    p.stepCount    = 5;

    p.steps[0] = { /*cyc*/      0, /*dur*/   400, /*freq*/ 0x16C0, /*pw*/ 0x0800,
                   /*wave*/ WaveformBits::kTriangle | WaveformBits::kGate,
                   /*ad*/ 0x05, /*sr*/ 0xAA, /*flo*/ 0, /*fhi*/ 0, /*fres*/ 0, /*modV*/ 0x0F,
                   /*fl*/ StepFlag::kHardRestartTransient };
    p.steps[1] = { /*cyc*/    400, /*dur*/  2000, /*freq*/ 0x1100, /*pw*/ 0x0800,
                   /*wave*/ WaveformBits::kTriangle | WaveformBits::kGate, 0x05, 0xAA, 0, 0, 0, 0x0F, 0 };
    p.steps[2] = { /*cyc*/   2400, /*dur*/  4000, /*freq*/ 0x0A00, /*pw*/ 0x0800,
                   /*wave*/ WaveformBits::kTriangle | WaveformBits::kGate, 0x05, 0xAA, 0, 0, 0, 0x0F, 0 };
    p.steps[3] = { /*cyc*/   6400, /*dur*/ 12000, /*freq*/ 0x0500, /*pw*/ 0x0800,
                   /*wave*/ WaveformBits::kTriangle | WaveformBits::kGate, 0x05, 0xAA, 0, 0, 0, 0x0F, 0 };
    p.steps[4] = { /*cyc*/  18400, /*dur*/  4000, /*freq*/ 0x0500, /*pw*/ 0x0800,
                   /*wave*/ WaveformBits::kTriangle, /*gate off*/ 0x05, 0xAA, 0, 0, 0, 0x0F,
                   /*fl*/ StepFlag::kIsTerminalStep };

    p.controls.tuneStepIndex   = 0;
    p.controls.tuneFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, freq));
    p.controls.decayStepIndex  = 3;
    p.controls.decayFieldOffset= static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    return p;
}

// Closed hat: short noise burst, gate-off on terminal step, NoiseShared/HiHat choke.
constexpr DrSidInstrumentProgram makeExemplarClosedHat() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::ClosedHat;
    p.chokeGroup   = ChokeGroup::HiHat;
    p.voicePolicy  = VoicePolicy::FixedVoice2;
    p.stepCount    = 2;

    p.steps[0] = { 0,    400, 0x4000, 0,
                   WaveformBits::kNoise | WaveformBits::kGate,
                   0x00, 0x11, 0, 0, 0, 0x0F, 0 };
    p.steps[1] = { 400, 1500, 0x4000, 0,
                   WaveformBits::kNoise,
                   0x00, 0x11, 0, 0, 0, 0x0F,
                   StepFlag::kIsTerminalStep };

    p.controls.decayStepIndex  = 1;
    p.controls.decayFieldOffset= static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    return p;
}

// Snare: short hard-restarted noise transient followed by a brighter body.
// Shares voice 1 with clap/tom in the compact three-voice DrSID map.
constexpr DrSidInstrumentProgram makeExemplarSnare() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::Snare;
    p.chokeGroup   = ChokeGroup::NoiseShared;
    p.voicePolicy  = VoicePolicy::FixedVoice1;
    p.stepCount    = 4;

    const std::uint8_t W_ON  = WaveformBits::kNoise | WaveformBits::kGate;
    const std::uint8_t W_OFF = WaveformBits::kNoise;
    p.steps[0] = {    0,  220, 0x4200, 0x0800, W_ON,  0x02, 0x66, 0, 0, 0x21, 0x1F,
                     StepFlag::kHardRestartTransient | StepFlag::kFilterRouteChange };
    p.steps[1] = {  220,  480, 0x3600, 0x0800, W_ON,  0x02, 0x55, 0, 0, 0x21, 0x1F, 0 };
    p.steps[2] = {  700, 2600, 0x3000, 0x0800, W_ON,  0x02, 0x33, 0, 0, 0x21, 0x1F, 0 };
    p.steps[3] = { 3300, 1400, 0x3000, 0x0800, W_OFF, 0x02, 0x33, 0, 0, 0x21, 0x0F,
                   StepFlag::kIsTerminalStep };

    p.controls.tuneStepIndex    = 1;
    p.controls.tuneFieldOffset  = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, freq));
    p.controls.decayStepIndex   = 2;
    p.controls.decayFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    p.controls.colorStepIndex   = 0;
    p.controls.colorFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, filterResRoute));
    return p;
}

// Open hat / cymbal family: longer noise tail in the HiHat choke group so
// closed-hat programs can deterministically steal it.
constexpr DrSidInstrumentProgram makeExemplarOpenHat() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::OpenHat;
    p.chokeGroup   = ChokeGroup::HiHat;
    p.voicePolicy  = VoicePolicy::FixedVoice2;
    p.stepCount    = 3;

    const std::uint8_t W_ON  = WaveformBits::kNoise | WaveformBits::kGate;
    const std::uint8_t W_OFF = WaveformBits::kNoise;
    p.steps[0] = {    0,  600, 0x6400, 0, W_ON,  0x00, 0x66, 0, 0, 0x82, 0x2F,
                     StepFlag::kFilterRouteChange };
    p.steps[1] = {  600, 8200, 0x5C00, 0, W_ON,  0x00, 0x55, 0, 0, 0x82, 0x2F, 0 };
    p.steps[2] = { 8800, 3000, 0x5C00, 0, W_OFF, 0x00, 0x44, 0, 0, 0x82, 0x0F,
                   StepFlag::kIsTerminalStep };

    p.controls.tuneStepIndex    = 0;
    p.controls.tuneFieldOffset  = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, freq));
    p.controls.decayStepIndex   = 1;
    p.controls.decayFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    p.controls.colorStepIndex   = 0;
    p.controls.colorFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, filterResRoute));
    return p;
}

// Clap: 3 quick noise bursts with slight de-tune (the "spread"), then noise tail.
// NoiseShared choke so clap and snare race the same voice when packed close.
constexpr DrSidInstrumentProgram makeExemplarClap() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::Clap;
    p.chokeGroup   = ChokeGroup::NoiseShared;
    p.voicePolicy  = VoicePolicy::FixedVoice1;
    p.stepCount    = 6;

    // Three burst pairs (gate-on / gate-off), then a longer noise tail.
    const std::uint8_t W_ON  = WaveformBits::kNoise | WaveformBits::kGate;
    const std::uint8_t W_OFF = WaveformBits::kNoise;
    p.steps[0] = {     0,  150, 0x6000, 0, W_ON,  0x00, 0x11, 0, 0, 0, 0x0F, 0 };
    p.steps[1] = {   150,  150, 0x6000, 0, W_OFF, 0x00, 0x11, 0, 0, 0, 0x0F, 0 };
    p.steps[2] = {   300,  150, 0x6000, 0, W_ON,  0x00, 0x22, 0, 0, 0, 0x0F, 0 };
    p.steps[3] = {   450,  150, 0x6000, 0, W_OFF, 0x00, 0x22, 0, 0, 0, 0x0F, 0 };
    p.steps[4] = {   600,  150, 0x6000, 0, W_ON,  0x00, 0x55, 0, 0, 0, 0x0F, 0 };
    p.steps[5] = {   750, 3000, 0x6000, 0, W_OFF, 0x00, 0x55, 0, 0, 0, 0x0F,
                    StepFlag::kIsTerminalStep };

    p.controls.colorStepIndex  = 2;
    p.controls.colorFieldOffset= static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, cycleOffset));
    p.controls.decayStepIndex  = 5;
    p.controls.decayFieldOffset= static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    return p;
}

// Cowbell / agogo: two pulse pitches on voice 0 with a short filter-routed tail.
constexpr DrSidInstrumentProgram makeExemplarCowbell() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::Cowbell;
    p.chokeGroup   = ChokeGroup::None;
    p.voicePolicy  = VoicePolicy::FixedVoice0;
    p.stepCount    = 4;

    const std::uint8_t W_ON  = WaveformBits::kPulse | WaveformBits::kGate;
    const std::uint8_t W_OFF = WaveformBits::kPulse;
    p.steps[0] = {    0,  500, 0x2300, 0x0200, W_ON,  0x00, 0x44, 0, 0, 0x41, 0x1F,
                     StepFlag::kFilterRouteChange };
    p.steps[1] = {  500, 1800, 0x2E00, 0x0500, W_ON,  0x00, 0x55, 0, 0, 0x41, 0x1F, 0 };
    p.steps[2] = { 2300, 5200, 0x2300, 0x0200, W_ON,  0x00, 0x66, 0, 0, 0x41, 0x1F, 0 };
    p.steps[3] = { 7500, 1800, 0x2300, 0x0200, W_OFF, 0x00, 0x44, 0, 0, 0x41, 0x0F,
                   StepFlag::kIsTerminalStep };

    p.controls.tuneStepIndex    = 0;
    p.controls.tuneFieldOffset  = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, freq));
    p.controls.decayStepIndex   = 2;
    p.controls.decayFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    return p;
}

// Tom / conga family: tonal triangle+pulse pitch drop on voice 1.
constexpr DrSidInstrumentProgram makeExemplarTom() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::Tom;
    p.chokeGroup   = ChokeGroup::TomShared;
    p.voicePolicy  = VoicePolicy::FixedVoice1;
    p.stepCount    = 5;

    const std::uint8_t W_ON  = WaveformBits::kTriangle | WaveformBits::kPulse | WaveformBits::kGate;
    const std::uint8_t W_OFF = WaveformBits::kTriangle | WaveformBits::kPulse;
    p.steps[0] = {    0,  360, 0x1800, 0x0600, W_ON,  0x03, 0x88, 0, 0, 0x12, 0x1F,
                     StepFlag::kHardRestartTransient | StepFlag::kFilterRouteChange };
    p.steps[1] = {  360, 1100, 0x1400, 0x0700, W_ON,  0x03, 0x88, 0, 0, 0x12, 0x1F, 0 };
    p.steps[2] = { 1460, 2600, 0x1000, 0x0800, W_ON,  0x03, 0x77, 0, 0, 0x12, 0x1F, 0 };
    p.steps[3] = { 4060, 6400, 0x0C00, 0x0900, W_ON,  0x03, 0x55, 0, 0, 0x12, 0x1F, 0 };
    p.steps[4] = {10460, 2200, 0x0C00, 0x0900, W_OFF, 0x03, 0x44, 0, 0, 0x12, 0x0F,
                  StepFlag::kIsTerminalStep };

    p.controls.tuneStepIndex    = 0;
    p.controls.tuneFieldOffset  = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, freq));
    p.controls.decayStepIndex   = 3;
    p.controls.decayFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, sustainRelease));
    return p;
}

// Rim / side-stick / woodblock: very short pulse transient on voice 2.
constexpr DrSidInstrumentProgram makeExemplarRim() noexcept {
    DrSidInstrumentProgram p{};
    p.drumClass    = SidGMDrumClass::Rim;
    p.chokeGroup   = ChokeGroup::None;
    p.voicePolicy  = VoicePolicy::FixedVoice2;
    p.stepCount    = 3;

    const std::uint8_t W_ON  = WaveformBits::kPulse | WaveformBits::kGate;
    const std::uint8_t W_OFF = WaveformBits::kPulse;
    p.steps[0] = {   0,  180, 0x5200, 0x0200, W_ON,  0x00, 0x11, 0, 0, 0x80, 0x1F,
                    StepFlag::kHardRestartTransient | StepFlag::kFilterRouteChange };
    p.steps[1] = { 180,  720, 0x4A00, 0x0300, W_ON,  0x00, 0x11, 0, 0, 0x80, 0x1F, 0 };
    p.steps[2] = { 900, 1200, 0x4A00, 0x0300, W_OFF, 0x00, 0x11, 0, 0, 0x80, 0x0F,
                  StepFlag::kIsTerminalStep };

    p.controls.tuneStepIndex   = 0;
    p.controls.tuneFieldOffset = static_cast<std::uint8_t>(offsetof(DrSidRegisterStep, freq));
    return p;
}

constexpr DrSidInstrumentProgram makeCanonicalDrSidProgram(SidGMDrumClass cls) noexcept {
    switch (cls) {
        case SidGMDrumClass::Kick:      return makeExemplarKick();
        case SidGMDrumClass::Snare:     return makeExemplarSnare();
        case SidGMDrumClass::ClosedHat: return makeExemplarClosedHat();
        case SidGMDrumClass::OpenHat:   return makeExemplarOpenHat();
        case SidGMDrumClass::Clap:      return makeExemplarClap();
        case SidGMDrumClass::Cowbell:   return makeExemplarCowbell();
        case SidGMDrumClass::Tom:       return makeExemplarTom();
        case SidGMDrumClass::Rim:       return makeExemplarRim();
        default:                        return DrSidInstrumentProgram{};
    }
}

constexpr std::array<DrSidInstrumentProgram, kCanonicalDrSidProgramCount>
makeCanonicalDrSidKitPrograms() noexcept {
    return {{
        makeExemplarKick(),
        makeExemplarSnare(),
        makeExemplarClosedHat(),
        makeExemplarOpenHat(),
        makeExemplarClap(),
        makeExemplarCowbell(),
        makeExemplarTom(),
        makeExemplarRim(),
    }};
}

// Compile-time validation of the exemplars. If any factory ever drifts out of
// contract (e.g. someone adds a non-monotonic cycleOffset or forgets the
// terminal flag) the build breaks here, *not* at runtime.
static_assert(programIsWellFormed(makeExemplarKick()),       "exemplar Kick must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarSnare()),      "exemplar Snare must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarClosedHat()), "exemplar ClosedHat must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarOpenHat()),   "exemplar OpenHat must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarClap()),       "exemplar Clap must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarCowbell()),    "exemplar Cowbell must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarTom()),        "exemplar Tom must satisfy programIsWellFormed");
static_assert(programIsWellFormed(makeExemplarRim()),        "exemplar Rim must satisfy programIsWellFormed");
static_assert(makeCanonicalDrSidKitPrograms().size() == kCanonicalDrSidProgramCount,
              "canonical DrSID kit program array must hold exactly 8 programs");

} // namespace Drsid
} // namespace ArpSID

#endif // ARPSID_CORE_DRSID_INSTRUMENT_PROGRAM_H
