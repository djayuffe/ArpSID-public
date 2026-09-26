// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_phi2_machine.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID::C64;

    C64Phi2Machine m;
    m.powerOn();

    // CIA1 Timer A IRQ must surface through PHI2 diagnostics.
    Phi2BusPhase ph{};
    m.memory().cpuWrite(0, 0xDC04, 0x01, ph, false);
    m.memory().cpuWrite(1, 0xDC05, 0x00, ph, false);
    m.memory().cpuWrite(2, 0xDC0D, 0x81, ph, false);
    m.memory().cpuWrite(3, 0xDC0E, 0x19, ph, false);
    m.runPhi2(3);
    auto d = m.diagnostics();
    require(d.cia1TimerAUnderflows >= 1u, "PHI2 diagnostics expose CIA1 Timer A underflow");
    require(d.cia1IrqEdges >= 1u, "PHI2 diagnostics expose CIA1 IRQ edge");
    require(d.cia1IrqLevel, "PHI2 diagnostics expose CIA1 IRQ level");

    // CIA2 FLAG/NMI-side state should also surface.
    m.cia2().pulseFlag();
    m.runPhi2(1);
    d = m.diagnostics();
    require(d.cia2FlagLatched, "PHI2 diagnostics expose CIA2 FLAG latch");

    // CIA1 TOD write stop/resume and alarm-write-mode must be visible through PHI2 diagnostics.
    // Physical 6526 write protocol: HOURS stops, TENTHS restarts.
    m.memory().cpuWrite(10, 0xDC0B, 0x01, ph, false);
    m.runPhi2(1);
    d = m.diagnostics();
    require(d.cia1TodStopped, "PHI2 diagnostics expose CIA1 TOD stopped state after HOURS write");

    m.memory().cpuWrite(11, 0xDC08, 0x05, ph, false);
    m.runPhi2(1);
    d = m.diagnostics();
    require(!d.cia1TodStopped, "PHI2 diagnostics expose CIA1 TOD resumed state after TENTHS write");

    m.memory().cpuWrite(12, 0xDC0F, 0x80, ph, false);
    m.runPhi2(1);
    d = m.diagnostics();
    require(d.cia1TodAlarmWriteMode, "PHI2 diagnostics expose CIA1 TOD alarm write mode");

    // CIA1 serial state should be visible.
    m.memory().cpuWrite(20, 0xDC0C, 0xA5, ph, false);
    m.runPhi2(1);
    d = m.diagnostics();
    require(d.cia1SerialBitsRemaining <= 8u, "PHI2 diagnostics expose CIA1 serial bit counter range");
    require(m.cia1().serialBitsRemaining() <= 8u, "CIA1 serial bit counter is directly observable");

    std::cout << "C64CiaPhi2IntegrationV620Tests PASS\n";
    return 0;
}
