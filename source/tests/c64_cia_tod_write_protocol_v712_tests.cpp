// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_cia.h"
#include <cstdio>
#include <cstdlib>

using ArpSID::C64::Cia6526;

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main() {
    Cia6526 cia;
    cia.reset();
    cia.setClockHz(985248u);
    cia.setTod50Hz(true);

    // Normal C64 TOD set order is HOURS, MINUTES, SECONDS, TENTHS.
    // Writing HOURS must stop the TOD; writing TENTHS must restart it.
    cia.write(0x0Bu, 0x01u); // 1 AM; stop TOD while high fields are updated
    cia.step(Cia6526::kTodPal50HzPhi2Period * 20u);
    require(cia.todHours() == 0x01u, "TOD hours write stores normalized BCD hour");
    require(cia.todMinutes() == 0x00u && cia.todSeconds() == 0x00u && cia.todTenths() == 0x00u,
            "TOD must not advance after HOURS write before TENTHS restart");
    require(cia.timerPhaseSnapshot().todStopped, "TOD snapshot must report stopped after HOURS write");

    cia.write(0x0Au, 0x23u);
    cia.write(0x09u, 0x45u);
    cia.step(Cia6526::kTodPal50HzPhi2Period * 20u);
    require(cia.todMinutes() == 0x23u && cia.todSeconds() == 0x45u && cia.todTenths() == 0x00u,
            "TOD minutes/seconds writes are stable while stopped");

    cia.write(0x08u, 0x06u); // restart at 1:23:45.6
    require(!cia.timerPhaseSnapshot().todStopped, "TOD snapshot must report running after TENTHS write");
    cia.step(Cia6526::kTodPal50HzPhi2Period * 5u);
    require(cia.todTenths() == 0x07u, "TOD advances one tenth after restart at PAL 50 Hz");
    require(cia.todSeconds() == 0x45u, "TOD seconds unchanged after one tenth");

    // Alarm writes are selected by CRB bit7 and must not stop/restart TOD.
    cia.write(0x0Fu, 0x80u);
    cia.write(0x0Bu, 0x02u);
    require(!cia.timerPhaseSnapshot().todStopped, "TOD alarm HOURS write must not stop live TOD");
    cia.write(0x08u, 0x00u);
    require(!cia.timerPhaseSnapshot().todStopped, "TOD alarm TENTHS write must not alter running state");

    std::puts("C64CiaTodWriteProtocolV712Tests PASS");
    return 0;
}
