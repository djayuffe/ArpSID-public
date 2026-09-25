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
    req(c64.scheduleProjectionMirrorCpuWrite(0xD418, 0x0F, 8), "schedule projection write");
    c64.clearScheduledProjectionWrites();
    c64.runCycles(16);
    req(c64.sidRegisterImage()[0x18] == 0x00,
        "cleared projection write must not replay later");

    req(c64.scheduleCpuWrite(0xD418, 0x07, 0), "schedule fresh write after clear");
    c64.runCycles(1);
    req(c64.sidRegisterImage()[0x18] == 0x07,
        "fresh write after clear must still be applied");

    std::cout << "C64ProjectionQueueClearV875Tests PASS\n";
    return 0;
}
