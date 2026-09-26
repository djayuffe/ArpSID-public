// Copyright (C) 2024-2026 Ulf Bertilsson
#include "factory_patch_params.h"
#include "parameter_ids.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;
    for (int slot : {127, 128, 149, 150, 179}) {
        SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        require(root.valid(), "state root valid");
        const float bank = sidStateRootParamValue(root, kParamBankSlot);
        const float program = sidStateRootParamValue(root, kParamProgram);
        require(canonicalFactorySlotFromNormalizedBankSlot(bank) == slot, "bank slot roundtrips through state root");
        require(canonicalFactorySlotFromNormalizedBankSlot(program) == slot, "factory program roundtrips through state root");
    }
    require(sidStateRootParamValue(makeFactoryPatchStateRootForSlot(149), kParamBankSlot) !=
            sidStateRootParamValue(makeFactoryPatchStateRootForSlot(127), kParamBankSlot),
            "state root slot 149 does not alias slot 127");
    require(sidStateRootParamValue(makeFactoryPatchStateRootForSlot(179), kParamBankSlot) !=
            sidStateRootParamValue(makeFactoryPatchStateRootForSlot(127), kParamBankSlot),
            "state root slot 179 does not alias slot 127");
    std::cout << "FactoryStateRootExtendedIdentityV666Tests PASS\n";
    return 0;
}
