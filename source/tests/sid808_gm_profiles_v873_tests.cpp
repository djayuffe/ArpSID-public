// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/sid808_gm_projection.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "Sid808GMProfilesV873Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

void renderFrames(ArpSID::DrumEngineHostBridge& bridge, int frames) {
    float l[24000]{};
    float r[24000]{};
    require(frames <= 24000, "test render buffer is large enough");
    bridge.processBlock(l, r, frames);
}

} // namespace

int main() {
    using namespace ArpSID;

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "test bridge loads SID808 factory slot");

    bridge.noteOn(SidGMDrumClass::Tom, 92u, 50u);
    renderFrames(bridge, 9000);
    const auto highTomProfile = bridge.sid808Engine().lastPercProfile();
    const auto highTomWave = bridge.sid808Engine().lastMicroStageWaveformForDrum(Sid808Drum::Tom);
    const auto highTomTail = bridge.sid808Engine().lastMicroStageFreqForDrum(Sid808Drum::Tom);
    require(highTomProfile == Sid808PercProfile::TomHigh, "GM high tom gets a TomHigh profile");
    require(highTomWave == 0x10u, "GM high tom keeps the triangle tom program");

    bridge.noteOn(SidGMDrumClass::Tom, 92u, 62u);
    renderFrames(bridge, 9000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::CongaMute,
            "GM mute high conga gets a CongaMute profile");
    require(bridge.sid808Engine().lastMicroStageWaveformForDrum(Sid808Drum::Tom) == 0x40u,
            "GM mute conga uses a pulse conga stage, not the generic triangle tom");
    require(bridge.sid808Engine().lastMicroStageFreqForDrum(Sid808Drum::Tom) != highTomTail,
            "GM conga stage tail differs from high tom tail");

    bridge.noteOn(SidGMDrumClass::Tom, 92u, 65u);
    renderFrames(bridge, 9000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::TimbaleHigh,
            "GM high timbale gets a TimbaleHigh profile");
    require(bridge.sid808Engine().lastMicroStageWaveformForDrum(Sid808Drum::Tom) == 0x40u,
            "GM timbale uses the sharper pulse profile");

    bridge.noteOn(SidGMDrumClass::Tom, 92u, 78u);
    renderFrames(bridge, 9000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::CuicaMute,
            "GM mute cuica gets a CuicaMute profile");
    require(bridge.sid808Engine().lastMicroStageFreqForDrum(Sid808Drum::Tom) >
                bridge.sid808Engine().lastInitialFreqForDrum(Sid808Drum::Tom),
            "GM cuica profile chirps upward instead of using the generic tom drop");

    bridge.noteOn(SidGMDrumClass::OpenHat, 92u, 81u);
    renderFrames(bridge, 24000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::Triangle,
            "GM open triangle gets a Triangle cymbal profile");
    require(bridge.sid808Engine().lastAppliedConfig().freq < 65535u,
            "high GM triangle note must not raw-semitone clamp to SID max frequency");
    require(bridge.sid808Engine().lastMicroStageWaveformForDrum(Sid808Drum::OpenHat) == 0x40u,
            "GM triangle uses a ringing pulse profile rather than generic open-hat noise");

    bridge.noteOn(SidGMDrumClass::OpenHat, 92u, 49u);
    renderFrames(bridge, 24000);
    const auto crashTail = bridge.sid808Engine().lastMicroStageFreqForDrum(Sid808Drum::OpenHat);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::Crash,
            "GM crash gets a Crash profile");

    bridge.noteOn(SidGMDrumClass::OpenHat, 92u, 51u);
    renderFrames(bridge, 24000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::Ride,
            "GM ride gets a Ride profile");
    require(bridge.sid808Engine().lastMicroStageFreqForDrum(Sid808Drum::OpenHat) != crashTail,
            "GM crash and ride have distinct staged tails");

    Sid808HitOverride kitStyle{};
    kitStyle.selectedFactorySlot = 120u;
    kitStyle.hasSelectedFactorySlot = true;
    bridge.noteOnAtWithOverride(4, SidGMDrumClass::Tom, 92u, 61u, kitStyle, true);
    renderFrames(bridge, 9000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::BongoLow,
            "scheduled KIT-style SID808 path also applies GM note profiles");

    std::cout << "Sid808GMProfilesV873Tests PASS\n";
    return 0;
}
