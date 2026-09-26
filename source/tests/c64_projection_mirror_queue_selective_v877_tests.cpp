// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_sid_projection_bridge.h"
#include <cstdlib>
#include <iostream>

static void req(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    ArpSID::C64::C64Platform c64;
    c64.reset(true);
    c64.bootFromResetVector();
    c64.startRealtimeSidCore();

    req(c64.sidRegisterImage()[0x18] == 0x00, "initial SID volume is zero");

    req(ArpSID::C64::projectSidHostTimedWriteThroughC64Bus(c64, 0x18, 0x0F,
                                                           0, 0, 48000.0,
                                                           static_cast<double>(ArpSID::C64::kPalPhi2Hz)),
        "sample-zero projection observer write is queued");
    c64.clearScheduledProjectionWrites();
    c64.runCycles(1);
    req(c64.sidRegisterImage()[0x18] == 0x00,
        "sample-zero observer write must be discardable before mirror advance");

    req(c64.scheduleCpuWrite(0xD418, 0x07, 0),
        "ordinary CPU write can share the same queue");
    req(ArpSID::C64::projectSidHostTimedWriteThroughC64Bus(c64, 0x18, 0x0F,
                                                           0, 0, 48000.0,
                                                           static_cast<double>(ArpSID::C64::kPalPhi2Hz)),
        "second projection write can share the same queue");
    c64.clearScheduledProjectionWrites();
    c64.runCycles(1);
    req(c64.sidRegisterImage()[0x18] == 0x07,
        "clearScheduledProjectionWrites must preserve ordinary scheduled CPU writes");

    req(ArpSID::C64::projectSidHostTimedWriteThroughC64Bus(c64, 0x18, 0x03,
                                                           0, 0, 48000.0,
                                                           static_cast<double>(ArpSID::C64::kPalPhi2Hz)),
        "fresh projection write after selective clear");
    c64.runCycles(1);
    req(c64.sidRegisterImage()[0x18] == 0x03,
        "projection write applies when mirror advances");

    std::cout << "C64ProjectionMirrorQueueSelectiveV877Tests PASS\n";
    return 0;
}
