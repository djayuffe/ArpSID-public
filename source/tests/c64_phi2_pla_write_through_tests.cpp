#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_sid_bus_sink.h"

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
    SidBusSink sid;
    m.attachSidSink(&sid);
    m.powerOn();

    Phi2BusPhase phase{};
    m.memory().pokeRam(0xD400u, 0x12u);
    m.port().write(0x0000u, 0x07u);
    m.port().write(0x0001u, 0x37u);
    m.memory().cpuWrite(0, 0xD400u, 0xFFu, phase);
    require(sid.writeCount() == 1, "visible D400 write reaches SID");
    require(sid.write(0).reg == 0x00u && sid.write(0).value == 0xFFu, "visible SID write records reg/value");
    require(m.memory().peekRam(0xD400u) == 0x12u, "visible SID write does not corrupt RAM under I/O");

    phase = Phi2BusPhase{};
    m.port().write(0x0001u, 0x00u);
    m.memory().cpuWrite(1, 0xD400u, 0xA5u, phase);
    require(sid.writeCount() == 1, "hidden D400 write does not reach SID");
    require(m.memory().peekRam(0xD400u) == 0xA5u, "hidden D400 write updates RAM under I/O");
    require(m.memory().ioHiddenSidStoresToRam() == 1, "hidden SID store telemetry increments");

    m.port().write(0x0001u, 0x37u);
    m.memory().pokeBasicRom(0xA000u, 0x99u);
    m.memory().pokeRam(0xA000u, 0x00u);
    phase = Phi2BusPhase{};
    m.memory().cpuWrite(2, 0xA000u, 0x42u, phase);
    phase = Phi2BusPhase{};
    require(m.memory().cpuRead(3, 0xA000u, phase) == 0x99u, "BASIC ROM-visible read sees ROM");
    m.port().write(0x0001u, 0x00u);
    phase = Phi2BusPhase{};
    require(m.memory().cpuRead(4, 0xA000u, phase) == 0x42u, "BASIC hidden read sees RAM write-through");

    m.port().write(0x0001u, 0x37u);
    m.openBus().drive(0xB0u);
    m.memory().pokeColorRam(0xD800u, 0x06u);
    phase = Phi2BusPhase{};
    require(m.memory().cpuRead(5, 0xD800u, phase) == 0xB6u, "color RAM upper nibble comes from open bus");

    m.openBus().drive(0xCCu);
    phase = Phi2BusPhase{};
    require(m.memory().cpuRead(6, 0xDE00u, phase) == 0xCCu, "unmapped I/O reads open bus");
    require(phase.openBusSource, "open bus phase is marked");

    std::cout << "c64_phi2_pla_write_through_tests PASS\n";
    return 0;
}

