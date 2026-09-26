// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_sid_bridge.h"
#include <cstdio>
#include <cstdlib>

using namespace ArpSID::C64;

static void require(bool v, const char* msg) {
    if (!v) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}

int main() {
    C64Platform p{};
    p.reset(true);
    p.cpuWrite(0xD418u, 0x0Au);
    const uint8_t openAfterWrite = p.readOpenBus();
    require(openAfterWrite == 0x0Au, "write drives open bus latch");
    require(p.cpuRead(0xD418u) == openAfterWrite, "write-only $D418 reads open bus without sink, not shadow register RAM");
    p.cpuWrite(0xD400u, 0x55u);
    require(p.cpuRead(0xD400u) == 0x55u, "write-only SID register returns current open bus after same-value bus write");
    require(p.cpuRead(0xD419u) == 0xFFu, "$D419 POTX disconnected returns high");
    require(p.cpuRead(0xD41Au) == 0xFFu, "$D41A POTY disconnected returns high");

    C64SidBridgeState bridge{};
    p.attachSid(&bridge);
    p.cpuWrite(0xD40Eu, 0x00u); // voice 3 frequency = $1000
    p.cpuWrite(0xD40Fu, 0x10u);
    p.cpuWrite(0xD413u, 0x00u); // fastest attack
    p.cpuWrite(0xD414u, 0x00u);
    p.cpuWrite(0xD412u, 0x21u); // saw + gate
    p.runCycles(100u);
    const uint8_t osc3 = p.cpuRead(0xD41Bu);
    const uint8_t env3 = p.cpuRead(0xD41Cu);
    require(osc3 != 0u, "$D41B OSC3 advances from the PHI2-clocked voice-3 oscillator");
    require(env3 != 0u, "$D41C ENV3 exposes the PHI2-clocked envelope counter");
    require(p.cpuRead(0xD419u) == 0xFFu, "bridge POTX remains disconnected high");

    bridge.resetTimedWrites();
    p.cpuWrite(0xD438u, 0x07u);
    require(bridge.d418WriteCount == 1u, "mirrored $D438 write hits $D418 bridge capture");
    require(bridge.timedWrites[0].reg == 0x18u && bridge.timedWrites[0].value == 0x07u, "mirror write is normalized to SID reg $18");
    std::puts("c64_sid_readback_physical_v711_tests: PASS");
    return 0;
}
