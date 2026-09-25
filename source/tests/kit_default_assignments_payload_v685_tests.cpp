#include "source/factory_patch_params.h"
#include "arpsid/gui/kit_panel_model.h"
#include <cstdlib>
#include <iostream>

static void req(bool ok, const char* msg){ if(!ok){ std::cerr << "FAIL: " << msg << "\n"; std::exit(1);} }
int main(){
    const auto m=ArpSID::GUI::makeDefaultKitPanelModel();
    req(ArpSID::GUI::kitPanelModelIsWellFormed(m), "default kit model well formed");
    for(std::uint8_t dc=0; dc<ArpSID::GUI::kKitDrumClassCount; ++dc){
        for(std::uint8_t et=0; et<ArpSID::GUI::kKitEngineTargetCount; ++et){
            const auto target=static_cast<ArpSID::GUI::KitEngineTarget>(et);
            const auto& a=m.drumAssignments[dc][et];
            const int slot=ArpSID::GUI::kitAbsoluteSlot(target, a.factorySlotIndex);
            const auto* pd=ArpSID::getFactoryPatchDefinition(slot);
            req(pd, "assignment patch definition exists");
            std::array<float, static_cast<size_t>(ArpSID::kNumParams)> params{};
            req(ArpSID::loadFactoryPatchNormalizedParamsForSlot(slot, params), "assignment params load");
            if(target==ArpSID::GUI::KitEngineTarget::DrSID){ req(params[static_cast<size_t>(ArpSID::kParamDrSidEnable)]>0.5f, "default DrSID assignment has DrSID payload"); }
            if(target==ArpSID::GUI::KitEngineTarget::SID808){ req(params[static_cast<size_t>(ArpSID::kParamDrSidEnable)]>0.5f, "default SID808 assignment has drum payload"); }
            if(target==ArpSID::GUI::KitEngineTarget::Digi){ req(params[static_cast<size_t>(ArpSID::kParamDrSidEnable)]<0.5f, "default Digi assignment is not DrSID"); }
        }
    }
    std::cout << "KitDefaultAssignmentsPayloadV685Tests PASS\n";
    return 0;
}
