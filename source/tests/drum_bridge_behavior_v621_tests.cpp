// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/gui/kit_sequencer.h"
#include "arpsid/gui/kit_voice_config.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "SID808 factory slot loads through bridge");
    require(bridge.activeIdentity().context == DrumContext::SID808_AnalogProjection,
            "bridge active context is SID808 after slot load");

    float mono[64];
    std::fill(std::begin(mono), std::end(mono), 123.0f);
    bridge.processBlock(mono, nullptr, 64);
    bool clearedOrRendered = true;
    for (float v : mono) {
        if (v == 123.0f) { clearedOrRendered = false; break; }
    }
    require(clearedOrRendered, "mono/null-right bridge render does not leave stale buffer");

    for (int i = 0; i < 128; ++i)
        bridge.noteOnAt(100 + i, SidGMDrumClass::Kick, 100, 36);
    require(bridge.scheduledNoteOverflowCount() > 0u,
            "scheduled-note overflow is counted");
    bridge.allNotesOff();
    bridge.resetScheduledNoteDiagnostics();

    // Compile target/slot metadata into KIT events.
    KitStepGrid grid = makeDefaultKitStepGrid();
    grid.steps[0][0] = 96;
    KitPanelModel model = makeDefaultKitPanelModel();
    model.activeEngineTarget = static_cast<std::uint8_t>(KitEngineTarget::SID808);
    model.drumAssignments[0][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex = 3;
    KitVoiceConfigGrid voiceGrid = makeDefaultKitVoiceConfigGrid();
    voiceGrid.voiceConfigs[0].waveform = 0x40u;
    voiceGrid.voiceConfigs[0].attackDecay = 0xA3u;
    voiceGrid.voiceConfigs[0].sustainRelease = 0xB4u;
    voiceGrid.voiceConfigs[0].pulseWidthLo = 0x77u;
    voiceGrid.voiceConfigs[0].pulseWidthHi = 0x07u;
    voiceGrid.voiceConfigs[0].flags = 0x07u;
    CompiledKitSequencer cs = makeDefaultCompiledKitSequencer();
    require(compileKitSequencer(cs, grid, model, voiceGrid), "kit compiles");
    require(cs.steps[0].eventCount == 1u, "kit event compiled");
    const auto& ev = cs.steps[0].events[0];
    require(ev.engineTarget == static_cast<std::uint8_t>(KitEngineTarget::SID808),
            "compiled KIT event carries engine target");
    const std::uint16_t slot = static_cast<std::uint16_t>(ev.selectedFactorySlotLo |
        (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
    require(slot == static_cast<std::uint16_t>(kSid808NewFactoryRange.first + 3u), "compiled KIT event carries selected factory slot");
    require(ev.voiceWaveform == 0x40u, "compiled KIT event carries voice waveform");
    require(ev.voiceAttackDecay == 0xA3u, "compiled KIT event carries voice AD");
    require(ev.voiceSustainRelease == 0xB4u, "compiled KIT event carries voice SR");
    require(static_cast<std::uint16_t>(ev.voicePulseWidthLo | (static_cast<std::uint16_t>(ev.voicePulseWidthHi) << 8)) == 0x0777u,
            "compiled KIT event carries pulse width");
    require(ev.voiceFlags == 0x07u, "compiled KIT event carries voice flags");

    std::cout << "DrumBridgeBehaviorV621Tests PASS\n";
    return 0;
}
