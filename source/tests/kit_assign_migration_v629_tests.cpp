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

    require(kKitAssignSchemaVersion == 2u, "KIT assign schema bumped for engineTargetOverride");

    KitAssignConfigGrid old{};
    old.schemaVersion = 1u;
    old.assignConfigs[0].digiSlotIndex = 5u;
    old.assignConfigs[0].tuneShiftBias = kKitAssignTuneBias;
    old.assignConfigs[0].flags = 0u;
    old.assignConfigs[0].engineTargetOverride = 0u; // historical padding byte: must NOT force DrSID

    kitAssignSanitizeGrid(old);
    require(old.schemaVersion == kKitAssignSchemaVersion, "old grid migrates to current schema");
    require(old.assignConfigs[0].engineTargetOverride == 255u,
            "old padding byte migrates to follow-global target");

    KitStepGrid grid = makeDefaultKitStepGrid();
    grid.steps[0][0] = 100u;

    KitPanelModel model = makeDefaultKitPanelModel();
    model.activeEngineTarget = static_cast<std::uint8_t>(KitEngineTarget::SID808);
    model.drumAssignments[0][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex = 2u;
    model.drumAssignments[0][static_cast<std::uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex = 9u;

    KitAssignConfigGrid legacy{};
    legacy.schemaVersion = 1u;
    legacy.assignConfigs[0].digiSlotIndex = 5u;
    legacy.assignConfigs[0].tuneShiftBias = kKitAssignTuneBias;
    legacy.assignConfigs[0].engineTargetOverride = 0u; // old pad; compile must treat as 255/follow global

    CompiledKitSequencer cs = makeDefaultCompiledKitSequencer();
    require(compileKitSequencer(cs, grid, model, makeDefaultKitVoiceConfigGrid(), legacy),
            "legacy assign grid compiles after migration");
    require(cs.steps[0].eventCount == 1u, "legacy migrated event exists");
    const auto& ev = cs.steps[0].events[0];
    require(ev.engineTarget == static_cast<std::uint8_t>(KitEngineTarget::SID808),
            "legacy padding byte does not force DrSID; follows global SID808");
    require(slotOf(ev) == static_cast<std::uint16_t>(kSid808NewFactoryRange.first + 2u),
            "legacy migrated event uses global SID808 assignment slot");

    KitAssignConfigGrid current = makeDefaultKitAssignConfigGrid();
    current.assignConfigs[0].engineTargetOverride = 0u;
    require(compileKitSequencer(cs, grid, model, makeDefaultKitVoiceConfigGrid(), current),
            "current assign grid with explicit override compiles");
    require(cs.steps[0].events[0].engineTarget == static_cast<std::uint8_t>(KitEngineTarget::DrSID),
            "current schema value 0 explicitly forces DrSID");

    std::cout << "KitAssignMigrationV629Tests PASS\n";
    return 0;
}
