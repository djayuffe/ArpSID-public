#include "arpsid/engines/digi_sampler_engine.h"
#include "arpsid/gui/gui_realtime_projection_v588.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <cmath>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;

    // Behavior: DigiSamplerEngine exposes one-shot triggerSlotAt with sub-block offset.
    DigiSamplerEngine digi;
    digi.prepare(48000.0);
    ArpSID::GUI::GuiRealtimeDigiProjection proj{};
    proj.schemaVersion = ArpSID::GUI::kGuiRealtimeProjectionSchemaVersion;
    proj.activeSlotCount = 1;
    proj.slots[0].sourceType = static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot);
    proj.slots[0].factorySlotIndex = 0;
    proj.slots[0].volume = 220;
    proj.slots[0].lengthScale = 0;
    proj.slots[0].stepVelocity = 110;
    ArpSID::GUI::DigiSampleBankBlob bank = ArpSID::GUI::makeDefaultDigiSampleBankBlob();

    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    digi.triggerSlotAt(0, 110, bank, proj, 16);
    digi.process(proj, bank, l.data(), r.data(), 64, false, false, 0);
    float pre = 0.0f, post = 0.0f;
    for (int i = 0; i < 16; ++i) pre += std::fabs(l[(std::size_t)i]);
    for (int i = 16; i < 64; ++i) post += std::fabs(l[(std::size_t)i]);
    require(pre < 1.0e-6f, "Digi triggerSlotAt is silent before requested offset");
    require(post > 1.0e-5f, "Digi triggerSlotAt produces audio after requested offset");

    // Source-shape checks for KIT->Digi routing and SID808 bridge policy are
    // performed by the packaging static checks; this executable pins behavior
    // of the Digi one-shot trigger itself.

    std::cout << "DigiKitBridgePolicyV622Tests PASS\n";
    return 0;
}
