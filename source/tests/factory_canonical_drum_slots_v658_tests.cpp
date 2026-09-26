// Copyright (C) 2024-2026 Ulf Bertilsson
#include "factory_patch_params.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/core/drum_context.h"
#include "parameter_ids.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;
    require(kFactoryPatchSlotCount >= 180, "factory state-root count covers canonical drum slots 0..179");

    for (int slot : {120, 124, 125, 127, 128, 149}) {
        const PatchDefinition* pd = getFactoryPatchDefinition(slot);
        require(pd != nullptr, "SID808 patch definition exists");
        require(!pd->id.empty(), "SID808 patch id authored");
        require(!pd->displayName.empty(), "SID808 patch name authored");
        require(pd->usage.role == PatchRole::Drum, "SID808 patch role is Drum");
        require(pd->staticState.drSidMode, "SID808 patch selects DrSID runtime authority");
        SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        require(root.valid(), "SID808 state root valid");
        require(sidStateRootParamValue(root, kParamBankSlot) == canonicalNormalizedBankSlotValue(slot),
                "SID808 state root preserves requested bank slot");
    }

    for (int slot : {150, 179}) {
        const PatchDefinition* pd = getFactoryPatchDefinition(slot);
        require(pd != nullptr, "Digi patch definition exists");
        require(pd->usage.role == PatchRole::Drum, "Digi patch role is Drum");
        SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        require(root.valid(), "Digi state root valid");
        require(sidStateRootParamValue(root, kParamBankSlot) == canonicalNormalizedBankSlotValue(slot),
                "Digi state root preserves requested bank slot");
    }

    std::cout << "FactoryCanonicalDrumSlotsV658Tests PASS\n";
    return 0;
}
