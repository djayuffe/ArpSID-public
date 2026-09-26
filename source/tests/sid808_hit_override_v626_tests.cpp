// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/sid808_engine.h"
#include "arpsid/engines/drum_engine_host_bridge.h"
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

    Sid808Engine e;
    e.prepare(48000.0);
    Sid808HitOverride ov{};
    ov.waveform = 0x40u;
    ov.attackDecay = 0xA3u;
    ov.sustainRelease = 0xB4u;
    ov.pulseWidth = 0x0777u;
    ov.flags = 0x07u;
    ov.hasWaveform = ov.hasAttackDecay = ov.hasSustainRelease =
        ov.hasPulseWidth = ov.hasFlags = true;

    e.noteOnWithOverride(Sid808Drum::Kick, 90, 36, ov);
    const auto cfg = e.lastAppliedConfig();
    require(cfg.waveform == 0x40u, "SID808 per-hit override applies waveform");
    require(cfg.attackDecay == 0xA3u, "SID808 per-hit override applies AD");
    require(cfg.sustainRelease == 0xB4u, "SID808 per-hit override applies SR");
    require(cfg.pulseWidth == 0x0777u, "SID808 per-hit override applies PW");
    require(cfg.flags == 0x07u, "SID808 per-hit override applies flags");

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "bridge loads SID808 context");
    bridge.noteOnAtWithOverride(8, SidGMDrumClass::Kick, 90, 36, ov, true);
    float l[32]{}, r[32]{};
    bridge.processBlock(l, r, 32);
    const auto routedCfg = bridge.sid808Engine().lastAppliedConfig();
    require(routedCfg.attackDecay == 0xA3u, "scheduled bridge override reaches router-owned SID808");
    require(routedCfg.pulseWidth == 0x0777u, "scheduled bridge override preserves PW");

    std::cout << "Sid808HitOverrideV626Tests PASS\n";
    return 0;
}
