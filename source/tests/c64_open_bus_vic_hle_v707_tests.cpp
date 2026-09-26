// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_open_bus.h"

#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;

    OpenBusLatch bus;
    bus.powerOn(0xFFu, 0u);
    bus.drive(0xA5u, 100u);
    bus.decayToPhi2(100u + 4096u);
    require(bus.drivenWithinPersistence(100u + 4096u), "open bus persists through old 4096-cycle cutoff");
    require(bus.value() != 0x00u, "open bus does not drop to zero before ~0x1D00 PHI2");
    bus.decayToPhi2(100u + OpenBusLatch::kPersistencePhi2);
    require(!bus.drivenWithinPersistence(100u + OpenBusLatch::kPersistencePhi2), "open bus persistence expires at configured window");
    require(bus.value() == 0x00u, "open bus deterministic decay reaches zero after persistence window");

    C64Phi2Machine m;
    m.powerOn();
    auto& mem = m.memory();
    require(mem.peekKernalRom(0xFFFAu) == 0x43u && mem.peekKernalRom(0xFFFBu) == 0xFEu,
            "HLE NMI vector points to $FE43");
    require(mem.peekKernalRom(0xFFFCu) == 0x00u && mem.peekKernalRom(0xFFFDu) == 0xE0u,
            "HLE RESET vector points to $E000");
    require(mem.peekKernalRom(0xFFFEu) == 0x48u && mem.peekKernalRom(0xFFFFu) == 0xFFu,
            "HLE IRQ vector points to $FF48");
    require(mem.peekKernalRom(0xFF48u) == 0x48u && mem.peekKernalRom(0xFF4Du) == 0x6Cu,
            "HLE IRQ entry saves registers then jumps through CINV");
    require(mem.peekKernalRom(0xFE43u) == 0x6Cu && mem.peekKernalRom(0xFE45u) == 0x03u,
            "HLE NMI entry jumps through NMINV");
    require(mem.peekKernalRom(0xEA31u) == 0xADu && mem.peekKernalRom(0xEA33u) == 0xDCu,
            "HLE default IRQ ACK reads CIA1 ICR");
    require(mem.peekKernalRom(0xFE47u) == 0xADu && mem.peekKernalRom(0xFE49u) == 0xDDu,
            "HLE default NMI ACK reads CIA2 ICR");

    VicII vic;
    vic.reset(true);
    vic.write(0x1Au, 0x01u); // enable raster IRQ
    vic.write(0x12u, 0x01u);
    for (unsigned i = 0; i < 64u; ++i) vic.tick();
    const uint8_t d019 = vic.read(0x19u);
    require((d019 & 0x70u) == 0x70u, "$D019 read has bits 4-6 high");
    require((d019 & 0x81u) == 0x81u, "$D019 read reports enabled latched raster IRQ and master bit");
    require(vic.irq(), "VIC IRQ line asserted when enabled raster IRQ is latched");
    vic.write(0x19u, 0x01u);
    require(!vic.irq(), "$D019 write-one-to-ack clears IRQ line");
    require((vic.read(0x19u) & 0x70u) == 0x70u, "$D019 fixed high bits remain after ACK");

    std::cout << "c64_open_bus_vic_hle_v707_tests PASS\n";
    return 0;
}
