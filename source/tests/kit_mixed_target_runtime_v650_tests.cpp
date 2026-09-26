// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/gui/kit_sequencer.h"
#include "arpsid/gui/kit_sid808_class_map.h"
#include "arpsid/gui/kit_panel_model.h"
#include "arpsid/gui/kit_step_grid.h"
#include "arpsid/gui/kit_assign_config.h"
#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/digi_sampler_engine.h"
#include "arpsid/gui/digi_sample_bank_v596.h"
#include "arpsid/core/drum_context.h"
#include <cstdlib>
#include <iostream>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::uint16_t slotOf(const ArpSID::GUI::CompiledKitEvent& ev) noexcept {
    return static_cast<std::uint16_t>(ev.selectedFactorySlotLo |
        (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    KitStepGrid grid = makeDefaultKitStepGrid();
    grid.steps[0][0] = 100; // Kick
    grid.steps[1][0] = 101; // Snare
    grid.steps[2][0] = 102; // ClosedHat

    KitPanelModel model = makeDefaultKitPanelModel();
    model.activeEngineTarget = static_cast<std::uint8_t>(KitEngineTarget::DrSID);
    model.drumAssignments[0][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex = 3;
    model.drumAssignments[1][static_cast<std::uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex = 4;
    model.drumAssignments[2][static_cast<std::uint8_t>(KitEngineTarget::Digi)].factorySlotIndex = 7;

    KitAssignConfigGrid assign = makeDefaultKitAssignConfigGrid();
    kitAssignSetEngineTargetOverride(assign.assignConfigs[0], static_cast<std::uint8_t>(KitEngineTarget::SID808));
    kitAssignSetEngineTargetOverride(assign.assignConfigs[1], static_cast<std::uint8_t>(KitEngineTarget::DrSID));
    kitAssignSetEngineTargetOverride(assign.assignConfigs[2], static_cast<std::uint8_t>(KitEngineTarget::Digi));

    CompiledKitSequencer cs = makeDefaultCompiledKitSequencer();
    require(compileKitSequencer(cs, grid, model, makeDefaultKitVoiceConfigGrid(), assign),
            "mixed target kit compiles");
    require(cs.steps[0].eventCount == 3u, "three mixed-target events compiled");

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    bridge.activateDefaultIdentityForContext(DrumContext::SID808_AnalogProjection);
    DrSidEngine drsid;
    drsid.setSampleRate(48000.0);
    DigiSamplerEngine digi;
    digi.prepare(48000.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();

    GuiRealtimeDigiProjection oneShot{};
    oneShot.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    oneShot.activeSlot = 0u;
    oneShot.activeSlotCount = 1u;
    oneShot.slots[0].sourceType = static_cast<std::uint8_t>(DigiSourceType::FactorySlot);
    oneShot.slots[0].factorySlotIndex = 7u;
    oneShot.slots[0].absoluteFactorySlot = static_cast<std::uint16_t>(kDigiNewFactoryRange.first + 7u);
    oneShot.slots[0].volume = 220u;
    oneShot.slots[0].stepVelocity = 102u;

    int sid808Hits = 0, drsidHits = 0, digiHits = 0;
    for (std::uint8_t i = 0; i < cs.steps[0].eventCount; ++i) {
        const auto& ev = cs.steps[0].events[i];
        const auto target = static_cast<KitEngineTarget>(ev.engineTarget);
        // v872: route via the production KitDrumClass->SidGMDrumClass converter, not
        // the raw cast (which swapped Rim/Cowbell and silenced Crash). Keeps this
        // test aligned with the render path so it cannot mask a routing regression.
        const auto gm = ArpSID::GUI::kitDrumClassToSidGM(ev.drumClass);

        if (target == KitEngineTarget::SID808) {
            Sid808HitOverride ov{};
            ov.selectedFactorySlot = slotOf(ev);
            ov.hasSelectedFactorySlot = true;
            bridge.noteOnWithOverride(gm, ev.velocity, ev.midiNote, ov);
            ++sid808Hits;
        } else if (target == KitEngineTarget::DrSID) {
            drsid.triggerKitMidiNote(ev.midiNote,
                                     static_cast<float>(ev.velocity) / 127.0f,
                                     slotOf(ev),
                                     true,
                                     ev.voiceWaveform, ev.voiceAttackDecay, ev.voiceSustainRelease,
                                     static_cast<std::uint16_t>(ev.voicePulseWidthLo |
                                        (static_cast<std::uint16_t>(ev.voicePulseWidthHi) << 8u)),
                                     ev.voiceFlags, ev.voiceOverrideMask);
            ++drsidHits;
        } else if (target == KitEngineTarget::Digi) {
            digi.triggerSlotAt(0u, ev.velocity, bank, oneShot, 0);
            ++digiHits;
        }
    }

    require(sid808Hits == 1, "exactly one SID808 event routed");
    require(drsidHits == 1, "exactly one DrSID event routed");
    require(digiHits == 1, "exactly one Digi event routed");
    require(bridge.sid808Engine().noteOnCount() == 1u, "SID808 engine received only kick");
    require(drsid.kitTriggerCount() == 1u, "DrSID engine received only snare");
    require(digi.telemetry().triggerCount == 1u, "Digi engine received only hat");

    float l[128]{}, r[128]{};
    bridge.processBlock(l, r, 128);
    std::vector<float> dl(128, 0.0f), dr(128, 0.0f);
    digi.process(oneShot, bank, dl.data(), dr.data(), 128, false, false, 0);
    float* outs[2] = {dl.data(), dr.data()};
    drsid.processReplacingBlock(outs, 128);

    float energy = 0.0f;
    for (int n = 0; n < 128; ++n)
        energy += std::abs(l[n]) + std::abs(r[n]) + std::abs(dl[n]) + std::abs(dr[n]);
    require(energy > 0.0f, "mixed target engines produced nonzero audio");

    std::cout << "KitMixedTargetRuntimeV650Tests PASS\n";
    return 0;
}
