#include "arpsid/engines/digi_sampler_engine.h"
#include "arpsid/gui/gui_realtime_projection_v588.h"
#include "arpsid/gui/digi_sample_bank_v596.h"
#include "arpsid/core/drum_context.h"
#include <cstdlib>
#include <iostream>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    DigiSamplerEngine sampler;
    sampler.prepare(48000.0);

    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();

    GuiRealtimeDigiProjection proj{};
    proj.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    proj.stepIndex = 3u;
    proj.activeSlot = 5u;
    proj.activeSlotCount = 6u;
    auto& s = proj.slots[5];
    s.activeAtStep = 1u;
    s.sourceType = static_cast<std::uint8_t>(DigiSourceType::FactorySlot);
    s.factorySlotIndex = 7u;
    s.absoluteFactorySlot = static_cast<std::uint16_t>(kDigiNewFactoryRange.first + 7u);
    s.stepVelocity = 100u;
    s.volume = 220u;

    sampler.triggerSlotAt(5u, 100u, bank, proj, 0);

    std::vector<float> l(128, 0.0f), r(128, 0.0f);
    sampler.process(proj, bank, l.data(), r.data(), static_cast<int>(l.size()), false, false, 0);

    const auto& tel = sampler.telemetry();
    require(tel.lastTriggeredSlot == 5u, "nonzero Digi slot remains the triggered slot");
    require(tel.lastTriggeredFactorySlot == static_cast<std::uint16_t>(kDigiNewFactoryRange.first + 7u),
            "nonzero Digi slot preserves selected absolute factory identity");
    require(tel.triggerCount >= 1u, "Digi trigger count increments");
    require(tel.playingVoiceCount >= 1u || tel.peakVoiceCount >= 1u,
            "Digi nonzero slot produces/created playback voice");

    float e = 0.0f;
    for (float v : l) e += std::abs(v);
    require(e > 0.0f, "Digi nonzero slot produced audio energy");

    std::cout << "DigiNonzeroSlotRuntimeV641Tests PASS\n";
    return 0;
}
