// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/drum_context.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "factory_patch_params.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;
    require(factorySlotContextForSchema(127, FactorySlotSchema::Legacy128) == DrumContext::DrSID_C64Wavetable,
            "legacy schema preserves slot 127 as DrSID");
    require(factorySlotContextForSchema(127, FactorySlotSchema::CanonicalV500Plus) == DrumContext::SID808_AnalogProjection,
            "canonical schema treats slot 127 as SID808");
    require(factorySlotContextForSchema(149, FactorySlotSchema::CanonicalV500Plus) == DrumContext::SID808_AnalogProjection,
            "canonical schema exposes SID808 slot 149");
    require(factorySlotContextForSchema(149, FactorySlotSchema::Legacy128) == DrumContext::None,
            "legacy schema does not invent slot 149");
    require(!isAuthoredDrSidProjectionFactorySlot(128), "authored DrSID helper must not alias 128 to 127");
    require(!isAuthoredDrSidProjectionFactorySlot(149), "authored DrSID helper must not alias SID808 149 to DrSID");
    std::array<float, static_cast<size_t>(kNumParams)> legacy120{};
    std::array<float, static_cast<size_t>(kNumParams)> canonical120{};
    require(loadFactoryPatchNormalizedParamsForSlotForSchema(120, FactorySlotSchema::Legacy128, legacy120),
            "legacy schema params load for slot 120");
    require(loadFactoryPatchNormalizedParamsForSlotForSchema(120, FactorySlotSchema::CanonicalV500Plus, canonical120),
            "canonical schema params load for slot 120");
    require(legacy120[static_cast<size_t>(kParamDrSidEnable)] > 0.5f &&
            legacy120[static_cast<size_t>(kParamDrSidMachineModel)] < 0.5f,
            "legacy schema params preserve slot 120 DrSID");
    require(canonical120[static_cast<size_t>(kParamDrSidEnable)] > 0.5f &&
            canonical120[static_cast<size_t>(kParamDrSidMachineModel)] > 0.5f,
            "canonical schema params preserve slot 120 SID808");
    SidStateRootV1 legacy127 = makeFactoryPatchStateRootForSlotForSchema(127, FactorySlotSchema::Legacy128);
    SidStateRootV1 canonical127 = makeFactoryPatchStateRootForSlotForSchema(127, FactorySlotSchema::CanonicalV500Plus);
    require(legacy127.document.current_program_ref.find(":legacy128") != std::string::npos,
            "legacy schema state root carries explicit legacy marker");
    require(canonical127.document.current_program_ref.find(":canonical") != std::string::npos,
            "canonical schema state root carries explicit canonical marker");

    std::array<float, static_cast<size_t>(kNumParams)> flatLegacyImport{};
    for (size_t i = 0; i < flatLegacyImport.size(); ++i) flatLegacyImport[i] = kParamInfos[i].defaultNorm;
    flatLegacyImport[static_cast<size_t>(kParamBankSlot)] = canonicalNormalizedBankSlotValue(127);
    SidStateRootV1 importedLegacy127 = importPresentationParamsToStateRootForSchema(flatLegacyImport.data(),
                                                                                     static_cast<int>(flatLegacyImport.size()),
                                                                                     FactorySlotSchema::Legacy128);
    SidStateRootV1 importedCanonical127 = importPresentationParamsToStateRootForSchema(flatLegacyImport.data(),
                                                                                        static_cast<int>(flatLegacyImport.size()),
                                                                                        FactorySlotSchema::CanonicalV500Plus);
    require(sidStateRootParamValue(importedLegacy127, kParamDrSidEnable) > 0.5f &&
            sidStateRootParamValue(importedLegacy127, kParamDrSidMachineModel) < 0.5f,
            "production legacy flat import restores slot 127 as DrSID");
    require(sidStateRootParamValue(importedCanonical127, kParamDrSidEnable) > 0.5f &&
            sidStateRootParamValue(importedCanonical127, kParamDrSidMachineModel) > 0.5f,
            "production canonical flat import restores slot 127 as SID808");
    std::cout << "FactorySlotSchemaMigrationV659Tests PASS\n";
    return 0;
}
