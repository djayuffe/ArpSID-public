#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/digi_sampler_engine.h"
#include "arpsid/patchbank/factory_sid808_kits.h"
#include "arpsid/gui/gui_realtime_projection_v588.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cmath>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;

    // SID808 selected factory slot is consumed per hit when no explicit voice
    // field overrides are present.
    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "bridge starts in SID808 context");

    Sid808HitOverride ov{};
    ov.selectedFactorySlot = 123u; // Hard kit
    ov.hasSelectedFactorySlot = true;
    bridge.noteOnWithOverride(SidGMDrumClass::Kick, 90, 36, ov);
    const Sid808KitConfigTable expected = factorySid808ResolvedKitForSlot(123);
    const auto got = bridge.sid808Engine().lastAppliedConfig();
    require(got.freq == expected[static_cast<std::size_t>(Sid808Drum::Kick)].freq,
            "selected SID808 factory slot supplies per-hit kick frequency");
    require(got.attackDecay == expected[static_cast<std::size_t>(Sid808Drum::Kick)].attackDecay,
            "selected SID808 factory slot supplies per-hit ADSR");

    // Explicit per-hit override wins over selected factory slot.
    Sid808HitOverride ov2{};
    ov2.selectedFactorySlot = 123u;
    ov2.hasSelectedFactorySlot = true;
    ov2.attackDecay = 0xA3u;
    ov2.hasAttackDecay = true;
    bridge.noteOnWithOverride(SidGMDrumClass::Kick, 90, 36, ov2);
    require(bridge.sid808Engine().lastAppliedConfig().attackDecay == 0xA3u,
            "explicit KIT voice override wins over selected factory slot");

    // Digi triggerSlotAt uses the projection slot index for voice allocation,
    // but telemetry and sample choice preserve the assigned absolute factory slot.
    DigiSamplerEngine digi;
    digi.prepare(48000.0);
    ArpSID::GUI::GuiRealtimeDigiProjection proj{};
    proj.schemaVersion = ArpSID::GUI::kGuiRealtimeProjectionSchemaVersion;
    proj.activeSlot = 5;
    proj.activeSlotCount = 1;
    auto& s = proj.slots[5];
    s.activeAtStep = 1;
    s.sourceType = static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot);
    s.factorySlotIndex = 7;
    s.absoluteFactorySlot = static_cast<std::uint16_t>(kDigiNewFactoryRange.first + 7u);
    s.stepVelocity = 110;
    s.volume = 220;
    s.lengthScale = 0;
    ArpSID::GUI::DigiSampleBankBlob bank = ArpSID::GUI::makeDefaultDigiSampleBankBlob();
    float l[64]{}, r[64]{};
    digi.triggerSlotAt(5, 110, bank, proj, 8);
    digi.process(proj, bank, l, r, 64, false, false, 0);
    require(digi.telemetry().lastTriggeredSlot == 5u,
            "Digi KIT trigger preserves runtime slot index");
    require(digi.telemetry().lastTriggeredFactorySlot == static_cast<std::uint16_t>(kDigiNewFactoryRange.first + 7u),
            "Digi KIT trigger preserves assigned absolute factory slot");

    std::cout << "KitSelectedSlotConsumptionV627Tests PASS\n";
    return 0;
}
