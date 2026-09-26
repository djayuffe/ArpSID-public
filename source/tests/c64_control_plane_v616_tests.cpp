// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_vic.h"
#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_psid_runtime.h"
#include <array>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::array<std::uint8_t, 0x7c + 10> makeBasicFlagRsid() {
    std::array<std::uint8_t, 0x7c + 10> data{};
    data[0] = 'R'; data[1] = 'S'; data[2] = 'I'; data[3] = 'D';
    data[0x04] = 0x00; data[0x05] = 0x02;
    data[0x06] = 0x00; data[0x07] = 0x7c;
    data[0x0e] = 0x00; data[0x0f] = 0x01;
    data[0x10] = 0x00; data[0x11] = 0x01;
    data[0x76] = 0x00; data[0x77] = 0x02; // BASIC flag
    data[0x7c + 0] = 0x00; data[0x7c + 1] = 0x10;
    data[0x7c + 2] = 0x60;
    return data;
}

int main() {
    using namespace ArpSID::C64;

    static_assert(Cia6526::kTimerIrqModelIsCycleExact, "CIA timer/IRQ core is resolved per PHI2");
    static_assert(VicII::kBusStealIsCycleExact, "VIC BA/AEC arbitration uses the cycle table");

    Cia6526 cia;
    cia.reset();
    cia.write(0x04, 0x01); cia.write(0x05, 0x00); // latch A = 1
    cia.write(0x0D, 0x81); // enable timer A IRQ
    cia.write(0x0E, 0x11); // start one-shot
    (void)cia.tick();
    auto cs = cia.timerPhaseSnapshot();
    require(cs.timerAUnderflows == 0u, "CIA Timer A latch=1 first tick only decrements");
    (void)cia.tick();
    cs = cia.timerPhaseSnapshot();
    require(cs.timerAUnderflows == 1u, "CIA Timer A underflow counted after decrement wraps through $FFFF");
    require(cs.irqEdges >= 1u, "CIA IRQ edge counted");

    VicII vic;
    vic.reset(true);
    vic.setMemoryBank(0);
    require(vic.fetchBase() == 0xC000u, "CIA2/VIC bank inversion model maps bank 0 to $C000");
    auto info = vic.cycleInfo(15);
    require(info.fetchAddress == 0xFFFFu || info.fetchAddress >= vic.fetchBase(), "VIC cycle table returns deterministic fetch address or idle");

    C64Phi2Machine m;
    m.powerOn();
    Phi2BusPhase phaseDdr{}; m.memory().cpuWrite(0, 0xDD02, 0x03, phaseDdr, false); // DDRA
    Phi2BusPhase phase{};
    m.memory().cpuWrite(1, 0xDD00, 0x00, phase, false); // CIA2 port A low two bits
    require(m.vic().memoryBank() == 0u, "CIA2 PA0/PA1 writes propagate to VIC bank");

    // PHI2 authority dirty-write reporting
    require(m.memory().dirtyWriteCount() >= 2u, "PHI2 memory matrix records dirty writes");
    require(m.memory().lastDirtyAddress() == 0xDD00u, "PHI2 memory matrix records last dirty address");

    // v893 refresh: RSID_BASIC images are now honestly refused at parse/load
    // (PsidParseResult::UnsupportedRsidBasic) instead of loading a pseudo-RSID
    // whose BASIC bootstrap can never execute. The control plane still must
    // never CLAIM BASIC startup execution.
    C64Runtime rt;
    auto img = makeBasicFlagRsid();
    require(!rt.loadPsid(img.data(), img.size()),
            "BASIC flag RSID is honestly refused at load");
    require(rt.lastLoadFailure() == PsidLoadFailure::UnsupportedRsidBasic,
            "BASIC flag RSID refusal is classed UnsupportedRsidBasic");
    require(!rt.basicStartupExecuted(), "BASIC startup execution is explicitly unsupported/not claimed");

    std::cout << "C64ControlPlaneV616Tests PASS\n";
    return 0;
}
