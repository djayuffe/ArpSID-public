// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_processor_port.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_d418_capture.h"
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

    // Exact 6510 $0001 read surface: high two bits are board-high and low
    // floating bits pull high, independent of stale open-bus contents.
    ProcessorPort6510 pp;
    pp.ddr = 0x00u;
    pp.data = 0x00u;
    require(pp.read(0x0001u, 0x00u) == 0xFFu, "processor port all inputs reads as pulled-high C64 $01");
    pp.ddr = 0xFFu;
    pp.data = 0x00u;
    require(pp.read(0x0001u, 0xFFu) == 0xC0u, "processor port high bits stay high even when DDR drives them low");
    pp.ddr = 0x07u;
    pp.data = 0x00u;
    require((pp.effectivePort() & 0x07u) == 0x00u, "processor port PLA bits obey PORT&DDR when outputs are enabled");

    C64Platform p;
    p.reset(true);
    // Public CPU bus path must expose the same $0001 readback as the standalone
    // helper and latch it onto open bus.
    p.cpuWrite(0x0000u, 0xFFu);
    p.cpuWrite(0x0001u, 0x00u);
    require(p.cpuRead(0x0001u) == 0xC0u, "C64Platform $0001 read uses exact C64 high-bit/floating-bit contract");

    // CIA2 PA0/PA1 write/DDRA changes immediately reselect the VIC bank through
    // the C64 physical decode path, not through a test-only helper.
    p.cpuWrite(0x0000u, 0x2Fu);
    p.cpuWrite(0x0001u, 0x37u); // I/O visible
    p.cpuWrite(0xDD02u, 0x03u); // CIA2 DDRA low two bits outputs
    p.cpuWrite(0xDD00u, 0x00u); // bank 0 -> $C000
    require(p.vic().memoryBank() == 0u && p.vic().fetchBase() == 0xC000u,
            "CIA2 PA0/PA1 output write immediately drives VIC bank 0/$C000");
    p.cpuWrite(0xDD00u, 0x03u); // bank 3 -> $0000
    require(p.vic().memoryBank() == 3u && p.vic().fetchBase() == 0x0000u,
            "CIA2 PA0/PA1 output write immediately drives VIC bank 3/$0000");
    p.cpuWrite(0xDD02u, 0x00u); // low bits become inputs pulled high by port input surface
    require((p.vic().memoryBank() & 0x03u) == 0x03u,
            "CIA2 DDRA change re-evaluates VIC bank with pulled-high input bits");

    // C64 physical SID mirror/write bus: $D418 must be captured every time even
    // when the same volume nibble is written repeatedly.
    C64SidBridgeState bridge;
    bridge.reset();
    p.attachSid(&bridge);
    p.cpuWrite(0xDD02u, 0x03u); // keep I/O visible and unrelated state deterministic
    p.cpuWrite(0xD418u, 0x0Cu);
    const auto c0 = p.lastSidWriteCycle();
    p.runCycles(3u);
    p.cpuWrite(0xD438u, 0x0Cu); // SID mirror: same reg 0x18, same value, later PHI2
    require(bridge.d418WriteCount == 2u, "C64Platform SID bus captures both $D418 and mirrored $D438 writes");
    require(bridge.d418RepeatedValueWriteCount == 1u, "C64Platform does not changed-filter repeated $D418 PCM samples");
    require(bridge.timedWriteCount >= 2u && bridge.timedWrites[0].phi2Cycle == c0,
            "$D418 bridge records absolute PHI2 timestamp from the C64 bus");

    float zoh[8]{};
    const size_t applied = reconstructD418Zoh(bridge, zoh, 8, kPalPhi2Hz, kPalPhi2Hz);
    require(applied == 2u, "Pure SID $D418 ZOH reconstruction applies repeated writes as events");

    std::cout << "C64SidcoreCompleteIntegrationV709Tests PASS\n";
    return 0;
}
