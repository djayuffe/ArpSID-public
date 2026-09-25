// SPDX-License-Identifier: BSD-3-Clause
// drsid_808_kit_data_v589_tests.cpp
//
// Pins the final DrSID/SID-808 audit guard:
// * live kick overlay base/sweep telemetry matches the SID core formulas
// across tune and accent values;
// * Tom remains canonical GM note 47 in engine and KIT data;
// * DrSID register-program data covers all 8 supported drum classes;
// * SID-808 factory lookup covers the whole canonical 120..149 range;
// * KIT defaults point every drum class at valid DrSID/SID-808/Digi ranges.

#include "arpsid/core/drsid_kit_compiler.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/gui/kit_panel_model.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

void requireNear(float actual, float expected, const char* msg, float eps = 0.5f) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > eps) {
        std::cerr << "FAIL: " << msg << " actual=" << actual
                  << " expected=" << expected << "\n";
        std::abort();
    }
}

void testLiveKickOverlayHzCoherence() {
    using ArpSID::DrSidEngine;

    const float tunes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float accents[] = {0.0f, 0.37f, 0.68f, 1.0f};

    for (float tune : tunes) {
        for (float accent : accents) {
            DrSidEngine x0x;
            x0x.setDrumMachineModelNormalized(1.0f);
            x0x.setKickTune(tune);
            x0x.setAccentAmount(accent);
            const float xBase = 35.0f + tune * 58.0f;
            const float xSweep = 121.0f + tune * 90.0f;
            const float xStart = 156.0f + tune * 148.0f;
            requireNear(x0x.kickOverlayBaseHzForTelemetry(), xBase,
                        "X0X8 live overlay base matches SID target Hz");
            requireNear(x0x.kickOverlaySweepDeltaHzForTelemetry(), xSweep,
                        "X0X8 live overlay sweep delta ignores accent drift");
            requireNear(x0x.kickOverlayBaseHzForTelemetry() +
                        x0x.kickOverlaySweepDeltaHzForTelemetry(), xStart,
                        "X0X8 live overlay sweep start matches SID start Hz");

            DrSidEngine auth;
            auth.setDrumMachineModelNormalized(0.0f);
            auth.setKickTune(tune);
            auth.setAccentAmount(accent);
            const float aBase = 42.0f + tune * 74.0f;
            const float aSweep = 108.0f + tune * 88.0f;
            const float aStart = 150.0f + tune * 162.0f;
            requireNear(auth.kickOverlayBaseHzForTelemetry(), aBase,
                        "Auth live overlay base matches SID target Hz");
            requireNear(auth.kickOverlaySweepDeltaHzForTelemetry(), aSweep,
                        "Auth live overlay sweep delta ignores accent drift");
            requireNear(auth.kickOverlayBaseHzForTelemetry() +
                        auth.kickOverlaySweepDeltaHzForTelemetry(), aStart,
                        "Auth live overlay sweep start matches SID start Hz");
        }
    }
}

void testTomCanonicalNote() {
    using ArpSID::DrSidEngine;
    using ArpSID::GUI::KitDrumClass;
    using ArpSID::GUI::kKitDrumClassMidiNote;

    const int engineTom =
        DrSidEngine::canonicalMidiNoteForDrumType(DrSidEngine::DrumType::Tom);
    const int kitTom =
        static_cast<int>(kKitDrumClassMidiNote[static_cast<int>(KitDrumClass::Tom)]);
    require(engineTom == 47, "engine Tom canonical MIDI note is 47");
    require(kitTom == 47, "KIT Tom MIDI note is 47");
    require(engineTom == kitTom, "engine and KIT Tom notes match");
}

void testCanonicalDrSidKitPrograms() {
    using namespace ArpSID;
    using namespace ArpSID::Drsid;

    constexpr auto programs = makeCanonicalDrSidKitPrograms();
    require(programs.size() == kCanonicalDrSidProgramCount,
            "canonical DrSID program kit has pinned size");

    CompiledDrSidKit compiled{};
    require(compileKit(programs.data(), programs.size(), 589u, compiled),
            "canonical DrSID program kit compiles");
    require(compiled.instrumentCount == kCanonicalDrSidProgramCount,
            "compiled DrSID kit preserves all 8 programs");

    const SidGMDrumClass classes[] = {
        SidGMDrumClass::Kick,
        SidGMDrumClass::Snare,
        SidGMDrumClass::ClosedHat,
        SidGMDrumClass::OpenHat,
        SidGMDrumClass::Clap,
        SidGMDrumClass::Cowbell,
        SidGMDrumClass::Tom,
        SidGMDrumClass::Rim,
    };
    for (SidGMDrumClass cls : classes) {
        const DrSidInstrumentProgram* p = findProgramForClass(compiled, cls);
        require(p != nullptr, "compiled DrSID kit has every class");
        require(programIsWellFormed(*p), "compiled DrSID program remains well-formed");
        require(p->fingerprint.expectedStepCount == p->stepCount,
                "compiled DrSID fingerprint pins step count");
        require(p->fingerprint.firstStepWord != 0u,
                "compiled DrSID fingerprint has first-step spot check");
    }
}

void testSid808AndKitAssignmentData() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    for (int slot = kSid808NewFactoryRange.first; slot <= kSid808NewFactoryRange.last; ++slot) {
        require(factorySlotContext(slot) == DrumContext::SID808_AnalogProjection,
                "SID-808 slot routes to SID-808 context");
        require(factorySid808KitForSlot(slot) != nullptr,
                "SID-808 slot has kit voice data");
        require(factorySid808KitName(slot) != nullptr,
                "SID-808 slot has display name");

        Sid808Engine engine;
        require(applyFactorySid808Kit(slot, engine),
                "SID-808 slot applies to engine");
        const auto kick = engine.drumVoiceConfig(Sid808Drum::Kick);
        require(kick.freq != 0u, "SID-808 applied kit has kick frequency");
        require(kick.voiceLevel > 0.0f, "SID-808 applied kit has audible kick level");
    }
    require(factorySid808KitForSlot(119) == nullptr, "slot 119 is not SID-808 kit data");
    require(factorySid808KitForSlot(150) == nullptr, "slot 150 is not SID-808 kit data");
    require(factorySid808KitVariantIndex(120) == 0, "SID-808 base bank starts at slot 120");
    require(factorySid808KitVariantIndex(125) == 1, "SID-808 slot 125 is a resolved kit variation");
    {
        Sid808Engine base;
        Sid808Engine varied;
        require(applyFactorySid808Kit(120, base), "slot 120 applies");
        require(applyFactorySid808Kit(125, varied), "slot 125 applies");
        const auto baseKick = base.drumVoiceConfig(Sid808Drum::Kick);
        const auto variedKick = varied.drumVoiceConfig(Sid808Drum::Kick);
        require(baseKick.freq != variedKick.freq ||
                baseKick.attackDecay != variedKick.attackDecay ||
                baseKick.sustainRelease != variedKick.sustainRelease ||
                baseKick.voiceLevel != variedKick.voiceLevel,
                "SID-808 repeated family slots resolve to distinct engine configs");
    }
    {
        Sid808Engine engine;
        require(applyFactorySid808Kit(120, engine), "slot 120 applies for 6581 compensation test");
        const auto rawKick = engine.drumVoiceConfig(Sid808Drum::Kick);
        const auto rawTom = engine.drumVoiceConfig(Sid808Drum::Tom);
        const auto rawSnare = engine.drumVoiceConfig(Sid808Drum::Snare);

        engine.setSidModel(SIDModel::MOS8580);
        const auto kick8580 = engine.compensatedDrumVoiceConfig(Sid808Drum::Kick);
        require(kick8580.freq == rawKick.freq,
                "8580 SID-808 kick uses raw factory pitch register");
        require(kick8580.pulseWidth == rawKick.pulseWidth,
                "8580 SID-808 kick uses raw factory pulse width");
        require(std::fabs(kick8580.voiceLevel - rawKick.voiceLevel) < 1.0e-6f,
                "8580 SID-808 kick uses raw factory level");

        engine.setSidModel(SIDModel::MOS6581);
        const auto kick6581 = engine.compensatedDrumVoiceConfig(Sid808Drum::Kick);
        const auto tom6581 = engine.compensatedDrumVoiceConfig(Sid808Drum::Tom);
        const auto snare6581 = engine.compensatedDrumVoiceConfig(Sid808Drum::Snare);
        require(kick6581.freq > rawKick.freq,
                "6581 SID-808 kick gets bass pitch compensation");
        require(kick6581.pulseWidth >= rawKick.pulseWidth,
                "6581 SID-808 kick keeps or widens pulse body");
        require(kick6581.voiceLevel >= rawKick.voiceLevel,
                "6581 SID-808 kick gets level compensation");
        require(tom6581.freq > rawTom.freq,
                "6581 SID-808 tom gets bass-family pitch compensation");
        require(tom6581.voiceLevel >= rawTom.voiceLevel,
                "6581 SID-808 tom gets level compensation");
        require(snare6581.freq == rawSnare.freq,
                "6581 SID-808 non-bass families do not retune pitch");
    }

    const KitPanelModel kit = makeDefaultKitPanelModel();
    require(kitPanelModelIsWellFormed(kit), "default KIT panel is well-formed");
    for (std::uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        const auto dr = kit.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex;
        const auto s8 = kit.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex;
        const auto dg = kit.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::Digi)].factorySlotIndex;
        require(isDrSidFactorySlot(kitAbsoluteSlot(KitEngineTarget::DrSID, dr)),
                "KIT DrSID assignment maps to DrSID factory range");
        require(isSid808FactorySlot(kitAbsoluteSlot(KitEngineTarget::SID808, s8)),
                "KIT SID-808 assignment maps to SID-808 factory range");
        require(factorySid808KitForSlot(kitAbsoluteSlot(KitEngineTarget::SID808, s8)) != nullptr,
                "KIT SID-808 assignment has concrete voice data");
        require(isDigiFactorySlot(kitAbsoluteSlot(KitEngineTarget::Digi, dg)),
                "KIT Digi assignment maps to Digi factory range");
    }
}

} // namespace

int main() {
    testLiveKickOverlayHzCoherence();
    testTomCanonicalNote();
    testCanonicalDrSidKitPrograms();
    testSid808AndKitAssignmentData();

    std::cout << "drsid_808_kit_data_v589_tests PASS\n";
    return 0;
}
