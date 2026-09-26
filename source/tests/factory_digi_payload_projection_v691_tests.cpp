// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/patchbank/factory_digi_kits.h"
#include "arpsid/patchbank/factory_digi_param_bridge.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}
int main() {
    using namespace ArpSID;
    for (int slot = 150; slot <= 179; ++slot) {
        const auto payload = makeFactoryDigiKitPayloadForSlot(slot);
        require(factoryDigiPayloadIsWellFormed(payload), "Digi factory payload is well-formed");
        require(payload.model.slots[0].sourceType == GUI::DigiSourceType::FactorySlot, "lane0 is factory source");
        require(GUI::digiStepIsActive(payload.model, 0, (std::uint8_t)((slot - 150) % GUI::kDigiStepCount)),
                "payload has deterministic audible step");

        const auto model = makeFactoryDigiPanelModelFromSignature(slot);
        const auto sig = factoryDigiParamSignatureForSlot(slot);
        require(model.slots[0].factorySlotIndex == sig.index, "signature index applied to Digi model");
        require(model.slots[0].startOffset > 0 || sig.startOffsetNorm == 0.0f, "start offset signature applied");
        require(model.slots[0].volume > 0, "volume signature applied");
        require(GUI::digiLoopEnabled(model.slots[0]) == sig.loop, "loop signature applied");
        require(GUI::digiReverseEnabled(model.slots[0]) == sig.reverse, "reverse signature applied");
    }

    const auto p150 = makeFactoryDigiKitPayloadForSlot(150);
    const auto p151 = makeFactoryDigiKitPayloadForSlot(151);
    const auto p157 = makeFactoryDigiKitPayloadForSlot(157);
    const auto p179 = makeFactoryDigiKitPayloadForSlot(179);
    require(p150.payloadHash != p151.payloadHash, "Digi slot 150 and 151 payloads differ");
    require(p150.payloadHash != p157.payloadHash, "Digi slot 150 and 157 payloads differ");
    require(p157.payloadHash != p179.payloadHash, "Digi slot 157 and 179 payloads differ");

    std::cout << "FactoryDigiPayloadProjectionV691Tests PASS\n";
    return 0;
}
