// Copyright (C) 2024-2026 Ulf Bertilsson
#include "source/factory_patch_params.h"
#include "arpsid/patchbank/factory_digi_param_bridge.h"
#include <cstdlib>
#include <iostream>

static void req(bool ok, const char* msg){ if(!ok){ std::cerr << "FAIL: " << msg << "\n"; std::exit(1);} }
int main(){
    for(int slot=150; slot<=179; ++slot){
        req(ArpSID::factorySlotContext(slot)==ArpSID::DrumContext::Digi4Bit, "slot context Digi");
        const auto* pd=ArpSID::getFactoryPatchDefinition(slot);
        req(pd, "Digi patch definition exists");
        req(pd->usage.role==ArpSID::PatchRole::Drum, "Digi role drum");
        req(!pd->staticState.drSidMode, "Digi not forced to DrSID");
        req(!pd->staticState.synthMode, "Digi synth mode false");
        const auto sig=ArpSID::factoryDigiParamSignatureForSlot(slot);
        req(sig.slot==slot, "Digi signature slot identity");
        std::array<float, static_cast<size_t>(ArpSID::kNumParams)> params{};
        req(ArpSID::loadFactoryPatchNormalizedParamsForSlot(slot, params), "Digi params load");
        req(params[static_cast<size_t>(ArpSID::kParamSynthModeEnable)] < 0.5f, "Digi synth param off");
        req(params[static_cast<size_t>(ArpSID::kParamDrSidEnable)] < 0.5f, "Digi DrSID param off");
        req(params[static_cast<size_t>(ArpSID::kParamSeqEnable)] > 0.5f, "Digi sequencer/default transport present");
        req(params[static_cast<size_t>(ArpSID::kParamBankSlot)] == ArpSID::canonicalNormalizedBankSlotValue(slot), "Digi bank identity");
        req(params[static_cast<size_t>(ArpSID::kParamProgram)] == ArpSID::canonicalNormalizedFactoryProgramValue(slot), "Digi program identity");
    }
    std::cout << "FactoryDigiCanonicalPayloadV684Tests PASS\n";
    return 0;
}
