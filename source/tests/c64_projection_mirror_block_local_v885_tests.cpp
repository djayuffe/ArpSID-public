// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
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

    req(c64.scheduleProjectionMirrorCpuWrite(0xD418, 0x04, 1),
        "early projection write scheduled inside consumed block");
    req(c64.scheduleProjectionMirrorCpuWrite(0xD418, 0x0F, 100),
        "late projection write scheduled beyond cycle-capped mirror advance");
    req(c64.scheduleCpuWrite(0xD418, 0x07, 100),
        "ordinary scheduled write at same late cycle is not block-local projection");

    (void)c64.runRealtimeSidCoreCycles(8, 256);
    req(c64.sidRegisterImage()[0x18] == 0x04,
        "early projection write applies when the consumed mirror advances far enough");

    c64.clearScheduledProjectionWrites();
    (void)c64.runRealtimeSidCoreCycles(128, 256);
    req(c64.sidRegisterImage()[0x18] == 0x07,
        "late projection write is discarded after consumed block, ordinary event survives");

    std::cout << "C64ProjectionMirrorBlockLocalV885Tests PASS\n";
    return 0;
}
