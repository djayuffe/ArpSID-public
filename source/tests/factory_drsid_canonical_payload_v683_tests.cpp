// Copyright (C) 2024-2026 Ulf Bertilsson
#include "source/factory_patch_params.h"
#include <cstdlib>
#include <iostream>

static void req(bool ok, const char* msg){ if(!ok){ std::cerr << "FAIL: " << msg << "\n"; std::exit(1);} }
int main(){
    for(int slot=80; slot<=119; ++slot){
        req(ArpSID::factorySlotContext(slot)==ArpSID::DrumContext::DrSID_C64Wavetable, "slot context DrSID");
        const auto* pd=ArpSID::getFactoryPatchDefinition(slot);
        req(pd, "patch definition exists");
        req(pd->usage.role==ArpSID::PatchRole::Drum, "DrSID role drum");
        req(pd->staticState.drSidMode, "DrSID static mode true");
        req(!pd->staticState.synthMode, "DrSID synth mode false");
        std::array<float, static_cast<size_t>(ArpSID::kNumParams)> params{};
        req(ArpSID::loadFactoryPatchNormalizedParamsForSlot(slot, params), "DrSID params load");
        req(params[static_cast<size_t>(ArpSID::kParamSynthModeEnable)] < 0.5f, "DrSID synth param off");
        req(params[static_cast<size_t>(ArpSID::kParamDrSidEnable)] > 0.5f, "DrSID enable param on");
        req(params[static_cast<size_t>(ArpSID::kParamBankSlot)] == ArpSID::canonicalNormalizedBankSlotValue(slot), "DrSID bank identity");
        req(params[static_cast<size_t>(ArpSID::kParamProgram)] == ArpSID::canonicalNormalizedFactoryProgramValue(slot), "DrSID program identity");
    }
    std::cout << "FactoryDrSidCanonicalPayloadV683Tests PASS\n";
    return 0;
}
