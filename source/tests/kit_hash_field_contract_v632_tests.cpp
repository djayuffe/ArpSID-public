// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/gui/kit_panel_model.h"
#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/gui/kit_assign_config.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::uint32_t localKitRelevantHash(const ArpSID::GUI::KitPanelModel& panel,
                                          const ArpSID::GUI::KitVoiceConfigGrid& voice,
                                          const ArpSID::GUI::KitAssignConfigGrid& assign) noexcept {
    std::uint32_t h = 2166136261u;
    const auto mix = [&h](std::uint8_t b) noexcept {
        h ^= static_cast<std::uint32_t>(b);
        h *= 16777619u;
    };
    for (std::uint8_t dc = 0; dc < ArpSID::GUI::kKitDrumClassCount; ++dc) {
        for (std::uint8_t et = 0; et < ArpSID::GUI::kKitEngineTargetCount; ++et) {
            const auto& a = panel.drumAssignments[dc][et];
            mix(a.factorySlotIndex);
            mix(a.reserved[0]);
            mix(a.reserved[1]);
            mix(a.reserved[2]);
        }
        const auto& ac = assign.assignConfigs[dc];
        mix(ac.digiSlotIndex);
        mix(ac.tuneShiftBias);
        mix(ac.startOffset);
        mix(ac.lengthScale);
        mix(ac.flags);
        mix(ac.engineTargetOverride);
        const auto& vc = voice.voiceConfigs[dc];
        mix(vc.waveform);
        mix(vc.attackDecay);
        mix(vc.sustainRelease);
        mix(vc.pulseWidthLo);
        mix(vc.pulseWidthHi);
        mix(vc.flags);
    }
    return h ? h : 1u;
}

int main() {
    using namespace ArpSID::GUI;
    static_assert(sizeof(KitDrumClassAssignment) == 4, "assignment layout pinned");
    static_assert(sizeof(KitVoiceConfig) == 8, "voice layout pinned");
    static_assert(std::is_trivially_copyable<KitDrumClassAssignment>::value, "assignment POD");
    static_assert(std::is_trivially_copyable<KitVoiceConfig>::value, "voice POD");

    KitPanelModel panel = makeDefaultKitPanelModel();
    KitVoiceConfigGrid voice = makeDefaultKitVoiceConfigGrid();
    KitAssignConfigGrid assign = makeDefaultKitAssignConfigGrid();

    const auto h0 = localKitRelevantHash(panel, voice, assign);
    panel.drumAssignments[0][0].factorySlotIndex ^= 1u;
    const auto h1 = localKitRelevantHash(panel, voice, assign);
    require(h0 != h1, "factorySlotIndex affects KIT hash");

    panel = makeDefaultKitPanelModel();
    voice.voiceConfigs[0].pulseWidthLo ^= 0x55u;
    const auto h2 = localKitRelevantHash(panel, voice, assign);
    require(h0 != h2, "pulseWidthLo affects KIT hash");

    voice = makeDefaultKitVoiceConfigGrid();
    voice.voiceConfigs[0].pulseWidthHi ^= 0x07u;
    const auto h3 = localKitRelevantHash(panel, voice, assign);
    require(h0 != h3, "pulseWidthHi affects KIT hash");

    assign.assignConfigs[0].engineTargetOverride = 2u;
    const auto h4 = localKitRelevantHash(panel, voice, assign);
    require(h0 != h4, "engineTargetOverride affects KIT hash");

    std::cout << "KitHashFieldContractV632Tests PASS\n";
    return 0;
}
