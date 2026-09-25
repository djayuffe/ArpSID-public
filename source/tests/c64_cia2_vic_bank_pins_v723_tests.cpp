#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_processor_port.h"
#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_vic.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static void testCia2PaPinsDriveVicBankWithoutGenericReadSideEffect() {
    ProcessorPort6510 port{};
    OpenBusLatch bus{};
    Cia6526 cia1{};
    Cia6526 cia2{};
    VicII vic{};
    MemoryMatrix mem{};
    port.powerOn();
    cia1.reset(); cia2.reset(); vic.reset(true); mem.powerOn(true);
    mem.attach(&port, &bus, nullptr, &cia1, &cia2, &vic);
    Phi2BusPhase phase{};

    // Inputs pulled high when DDRA bits are zero: PA0/PA1 read as 1/1 => bank 3.
    mem.cpuWrite(1, 0xDD02u, 0x00u, phase);
    require(vic.memoryBank() == 3u, "CIA2 DDRA input bits pull high into VIC bank 3");

    // Drive PA0/PA1 low as outputs: bank 0.
    mem.cpuWrite(2, 0xDD00u, 0x00u, phase);
    mem.cpuWrite(3, 0xDD02u, 0x03u, phase);
    require(vic.memoryBank() == 0u, "CIA2 PA0/PA1 driven low selects VIC bank 0");

    // Mixed DDR: PA0 output low, PA1 input pulled high => bank bit pattern 10b = 2.
    mem.cpuWrite(4, 0xDD00u, 0x00u, phase);
    mem.cpuWrite(5, 0xDD02u, 0x01u, phase);
    require(vic.memoryBank() == 2u, "CIA2 PA pins honor DDR and pulled input bits");

    // Writing PRA with DDRA=1 for PA0 only drives PA0; PA1 remains pulled high.
    mem.cpuWrite(6, 0xDD00u, 0x01u, phase);
    require(vic.memoryBank() == 3u, "CIA2 PRA re-evaluates VIC bank using side-effect-free pins");
}

int main() {
    testCia2PaPinsDriveVicBankWithoutGenericReadSideEffect();
    std::cout << "C64Cia2VicBankPinsV723Tests PASS\n";
    return 0;
}
