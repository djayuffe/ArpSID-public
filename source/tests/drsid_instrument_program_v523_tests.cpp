// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// drsid_instrument_program_v523_tests.cpp
//
// Pins the DrSID register-microprogram contract (Audit DrSID §3, §4B):
// * DrSidRegisterStep is exactly 16 bytes and trivially copyable.
// * DrSidInstrumentProgram is trivially copyable and stays within the
// pinned size envelope.
// * `programIsWellFormed` rejects every audit-required malformation:
// out-of-range stepCount, non-monotonic cycleOffset, missing terminal
// flag, schemaVersion drift, Unsupported drumClass, oversized pulseWidth.
// * The exemplar programs satisfy the contract — both at compile time
// (static_assert in the header) and at runtime (the asserts below).
// * The v589 canonical DrSID kit data covers every supported 8-class drum
// family, not just the original Kick / ClosedHat / Clap exemplars.
// * Voice policy and choke group are properly tagged on the exemplars
// according to the audit's drum-machine identity rules
// (closed hat ↔ HiHat choke, clap ↔ NoiseShared choke).

#include "arpsid/core/drsid_instrument_program.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID::Drsid;
    using ArpSID::SidGMDrumClass;

    // ── A. Layout pins (matches header static_asserts) ──────────────────────
    require(sizeof(DrSidRegisterStep)       == 16,  "DrSidRegisterStep is 16 bytes at runtime");
    require(sizeof(DrSidControlMap)         == 8,   "DrSidControlMap is 8 bytes at runtime");
    require(sizeof(DrSidExpectedFingerprint)== 16,  "DrSidExpectedFingerprint is 16 bytes at runtime");
    require(kMaxSteps == 32,                        "kMaxSteps pinned at 32");
    require(kProgramSchemaVersion == 1u,            "kProgramSchemaVersion starts at 1");

    // ── B. Default-constructed program is invalid (defensive default) ──────
    {
        DrSidInstrumentProgram empty{};
        require(empty.empty(),                              "default program has stepCount==0");
        require(!programIsWellFormed(empty),                "empty program fails programIsWellFormed");
        require(empty.schemaVersion == kProgramSchemaVersion,
                "default schemaVersion matches build constant");
        require(empty.chokeGroup  == ChokeGroup::None,      "default chokeGroup is None");
        require(empty.voicePolicy == VoicePolicy::AnyFree,  "default voicePolicy is AnyFree");
    }

    // ── C. Exemplar Kick ────────────────────────────────────────────────────
    {
        constexpr auto kick = makeExemplarKick();
        require(programIsWellFormed(kick),                  "exemplar Kick is well-formed (runtime)");
        require(kick.drumClass    == SidGMDrumClass::Kick,  "Kick drumClass tag");
        require(kick.chokeGroup   == ChokeGroup::None,      "Kick has no choke group");
        require(kick.voicePolicy  == VoicePolicy::FixedVoice0,
                "Kick is voice-0 (SID kick on classic C64 mappings)");
        require(kick.stepCount    == 5,                     "Kick has 5 steps");
        require((kick.steps[0].flags & StepFlag::kHardRestartTransient) != 0,
                "Kick step 0 carries the hard-restart transient flag");
        require((kick.steps[4].flags & StepFlag::kIsTerminalStep) != 0,
                "Kick terminal step carries kIsTerminalStep");
        // Monotonic cycle order
        for (std::uint8_t i = 1; i < kick.stepCount; ++i) {
            require(kick.steps[i].cycleOffset >= kick.steps[i-1].cycleOffset,
                    "Kick cycleOffset is non-decreasing");
        }
        // Pitch DROP: step 0 frequency must be strictly higher than the final
        // pre-terminal step. This is the audit-required "fast downward pitch
        // table" for authentic C64 kick behavior.
        require(kick.steps[0].freq > kick.steps[3].freq,
                "Kick pitch drops from step 0 to step 3 (audit §4B authentic kick law)");
    }

    // ── D. Exemplar ClosedHat ───────────────────────────────────────────────
    {
        constexpr auto hat = makeExemplarClosedHat();
        require(programIsWellFormed(hat),                   "exemplar ClosedHat well-formed");
        require(hat.drumClass   == SidGMDrumClass::ClosedHat, "ClosedHat tag");
        require(hat.chokeGroup  == ChokeGroup::HiHat,
                "closed hat MUST be in HiHat choke group (audit §4C non-negotiable)");
        require(hat.voicePolicy == VoicePolicy::FixedVoice2,
                "ClosedHat is voice-2 (hats/crash/noise on classic mapping)");
        require(hat.stepCount   == 2,                       "ClosedHat has 2 steps");
        // Noise must be the waveform for step 0.
        require((hat.steps[0].waveform & WaveformBits::kNoise) != 0,
                "ClosedHat step 0 uses NOISE waveform");
        require((hat.steps[0].waveform & WaveformBits::kGate) != 0,
                "ClosedHat step 0 gates on");
        require((hat.steps[1].waveform & WaveformBits::kGate) == 0,
                "ClosedHat terminal step releases the gate");
    }

    // ── E. Exemplar Clap (3 bursts) ─────────────────────────────────────────
    {
        constexpr auto clap = makeExemplarClap();
        require(programIsWellFormed(clap),                  "exemplar Clap well-formed");
        require(clap.drumClass  == SidGMDrumClass::Clap,    "Clap tag");
        require(clap.chokeGroup == ChokeGroup::NoiseShared,
                "Clap shares noise voice (audit §4B clap law)");
        require(clap.stepCount  == 6,                       "Clap has 6 steps (3 gate-on/off pairs + tail)");
        // Count gate-on transitions among the first 5 steps — must be 3 (the three bursts).
        int gateOnCount = 0;
        for (std::uint8_t i = 0; i < 5; ++i) {
            if ((clap.steps[i].waveform & WaveformBits::kGate) != 0) ++gateOnCount;
        }
        require(gateOnCount == 3, "Clap fires exactly three noise bursts (audit-correct multi-burst behavior)");
    }

    // ── F. programIsWellFormed REJECTS every malformation ──────────────────
    {
        auto good = makeExemplarKick();

        // 1. stepCount == 0
        {
            DrSidInstrumentProgram bad = good;
            bad.stepCount = 0;
            require(!programIsWellFormed(bad),              "rejects stepCount == 0");
        }
        // 2. stepCount > kMaxSteps
        {
            DrSidInstrumentProgram bad = good;
            bad.stepCount = kMaxSteps + 1;
            require(!programIsWellFormed(bad),              "rejects stepCount > kMaxSteps");
        }
        // 3. non-monotonic cycleOffset
        {
            DrSidInstrumentProgram bad = good;
            bad.steps[2].cycleOffset = 0;  // way before step 1
            require(!programIsWellFormed(bad),              "rejects non-monotonic cycleOffset");
        }
        // 4. terminal flag on non-last step (and missing on actual last)
        {
            DrSidInstrumentProgram bad = good;
            bad.steps[0].flags |= StepFlag::kIsTerminalStep;
            require(!programIsWellFormed(bad),              "rejects terminal flag set on non-last step");
        }
        {
            DrSidInstrumentProgram bad = good;
            bad.steps[bad.stepCount - 1].flags &= ~StepFlag::kIsTerminalStep;
            require(!programIsWellFormed(bad),              "rejects missing terminal flag on last step");
        }
        // 5. Unsupported drumClass
        {
            DrSidInstrumentProgram bad = good;
            bad.drumClass = SidGMDrumClass::Unsupported;
            require(!programIsWellFormed(bad),              "rejects Unsupported drumClass");
        }
        // 6. schemaVersion drift
        {
            DrSidInstrumentProgram bad = good;
            bad.schemaVersion = 99u;
            require(!programIsWellFormed(bad),              "rejects schemaVersion drift");
        }
        // 7. pulseWidth out of 12-bit range
        {
            DrSidInstrumentProgram bad = good;
            bad.steps[0].pulseWidth = 0xFFFFu;
            require(!programIsWellFormed(bad),              "rejects pulseWidth > 12-bit");
        }
    }

    // ── F2. v589 canonical 8-class DrSID kit data ─────────────────────────
    {
        constexpr auto snare   = makeExemplarSnare();
        constexpr auto openHat = makeExemplarOpenHat();
        constexpr auto cowbell = makeExemplarCowbell();
        constexpr auto tom     = makeExemplarTom();
        constexpr auto rim     = makeExemplarRim();

        require(programIsWellFormed(snare),   "exemplar Snare well-formed");
        require(programIsWellFormed(openHat), "exemplar OpenHat well-formed");
        require(programIsWellFormed(cowbell), "exemplar Cowbell well-formed");
        require(programIsWellFormed(tom),     "exemplar Tom well-formed");
        require(programIsWellFormed(rim),     "exemplar Rim well-formed");

        require(snare.drumClass == SidGMDrumClass::Snare &&
                snare.chokeGroup == ChokeGroup::NoiseShared &&
                snare.voicePolicy == VoicePolicy::FixedVoice1,
                "Snare data uses voice-1 noise-shared policy");
        require(openHat.drumClass == SidGMDrumClass::OpenHat &&
                openHat.chokeGroup == ChokeGroup::HiHat &&
                openHat.voicePolicy == VoicePolicy::FixedVoice2,
                "OpenHat data uses voice-2 hihat choke policy");
        require(cowbell.drumClass == SidGMDrumClass::Cowbell &&
                cowbell.voicePolicy == VoicePolicy::FixedVoice0,
                "Cowbell data uses voice-0 tonal policy");
        require(tom.drumClass == SidGMDrumClass::Tom &&
                tom.chokeGroup == ChokeGroup::TomShared &&
                tom.voicePolicy == VoicePolicy::FixedVoice1,
                "Tom data uses voice-1 tom-shared policy");
        require(rim.drumClass == SidGMDrumClass::Rim &&
                rim.voicePolicy == VoicePolicy::FixedVoice2,
                "Rim data uses voice-2 short-hit policy");

        constexpr auto kit = makeCanonicalDrSidKitPrograms();
        require(kit.size() == kCanonicalDrSidProgramCount,
                "canonical DrSID kit has the pinned 8 programs");
        bool seen[8]{};
        for (const auto& p : kit) {
            require(programIsWellFormed(p), "every canonical DrSID kit program is well-formed");
            const auto idx = static_cast<unsigned>(p.drumClass);
            require(idx < 8u, "canonical DrSID class index in range");
            require(!seen[idx], "canonical DrSID kit has no duplicate class");
            seen[idx] = true;
        }
        for (bool ok : seen) require(ok, "canonical DrSID kit covers every supported class");
    }

    // ── G. Control map sentinel semantics ──────────────────────────────────
    {
        const DrSidControlMap empty{};
        require(empty.tuneStepIndex   == 0xFFu &&
                empty.tuneFieldOffset == 0xFFu,
                "default control map sentinels are 0xFF (unbound)");
        const auto kick = makeExemplarKick();
        require(kick.controls.tuneStepIndex == 0,
                "Kick.Tune binds to step 0's freq field");
        require(kick.controls.tuneFieldOffset == offsetof(DrSidRegisterStep, freq),
                "Kick.Tune binds to the `freq` field offset");
        require(kick.controls.decayStepIndex == 3,
                "Kick.Decay binds to the step that holds the long sustain tail");
        require(kick.controls.decayFieldOffset == offsetof(DrSidRegisterStep, sustainRelease),
                "Kick.Decay binds to the `sustainRelease` ADSR field");
    }

    // ── H. WaveformBits / StepFlag bit values are stable ───────────────────
    {
        // These constants leak into the serialized kit cache — any drift
        // would silently invalidate every cached kit.
        require(WaveformBits::kGate     == 0x01,            "WaveformBits::kGate stable");
        require(WaveformBits::kTriangle == 0x10,            "WaveformBits::kTriangle stable");
        require(WaveformBits::kSawtooth == 0x20,            "WaveformBits::kSawtooth stable");
        require(WaveformBits::kPulse    == 0x40,            "WaveformBits::kPulse stable");
        require(WaveformBits::kNoise    == 0x80,            "WaveformBits::kNoise stable");
        require(StepFlag::kHardRestartTransient == 0x01,    "StepFlag::kHardRestartTransient stable");
        require(StepFlag::kIsTerminalStep       == 0x80,    "StepFlag::kIsTerminalStep stable");
    }

    std::cout << "drsid_instrument_program_v523_tests: register-microprogram contract pinned (audit DrSID §3/§4B)\n";
    return 0;
}
