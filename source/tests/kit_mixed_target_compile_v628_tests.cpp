#include "arpsid/gui/kit_sequencer.h"
#include "arpsid/gui/kit_assign_config.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::uint16_t slotOf(const ArpSID::GUI::CompiledKitEvent& ev) {
    return static_cast<std::uint16_t>(ev.selectedFactorySlotLo |
        (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    KitStepGrid grid = makeDefaultKitStepGrid();
    grid.steps[0][0] = 100; // Kick
    grid.steps[1][0] = 101; // Snare
    grid.steps[2][0] = 102; // ClosedHat

    KitPanelModel model = makeDefaultKitPanelModel();
    model.activeEngineTarget = static_cast<std::uint8_t>(KitEngineTarget::DrSID); // global fallback

    model.drumAssignments[0][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex = 3;
    model.drumAssignments[1][static_cast<std::uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex = 4;
    model.drumAssignments[2][static_cast<std::uint8_t>(KitEngineTarget::Digi)].factorySlotIndex = 7;

    KitAssignConfigGrid assign = makeDefaultKitAssignConfigGrid();
    kitAssignSetEngineTargetOverride(assign.assignConfigs[0], static_cast<std::uint8_t>(KitEngineTarget::SID808));
    kitAssignSetEngineTargetOverride(assign.assignConfigs[1], static_cast<std::uint8_t>(KitEngineTarget::DrSID));
    kitAssignSetEngineTargetOverride(assign.assignConfigs[2], static_cast<std::uint8_t>(KitEngineTarget::Digi));

    CompiledKitSequencer cs = makeDefaultCompiledKitSequencer();
    require(compileKitSequencer(cs, grid, model, makeDefaultKitVoiceConfigGrid(), assign),
            "mixed target kit compiles");
    require(cs.steps[0].eventCount == 3u, "mixed target step compiles all three events");

    const auto& kick = cs.steps[0].events[0];
    const auto& snare = cs.steps[0].events[1];
    const auto& hat = cs.steps[0].events[2];

    require(kick.engineTarget == static_cast<std::uint8_t>(KitEngineTarget::SID808),
            "kick event routes to SID808 override");
    require(slotOf(kick) == static_cast<std::uint16_t>(kSid808NewFactoryRange.first + 3u),
            "kick event uses SID808 assignment slot");

    require(snare.engineTarget == static_cast<std::uint8_t>(KitEngineTarget::DrSID),
            "snare event routes to DrSID override");
    require(slotOf(snare) == static_cast<std::uint16_t>(kDrSidNewFactoryRange.first + 4u),
            "snare event uses DrSID assignment slot");

    require(hat.engineTarget == static_cast<std::uint8_t>(KitEngineTarget::Digi),
            "hat event routes to Digi override");
    require(slotOf(hat) == static_cast<std::uint16_t>(kDigiNewFactoryRange.first + 7u),
            "hat event uses Digi assignment slot");

    std::cout << "KitMixedTargetCompileV628Tests PASS\n";
    return 0;
}
