#include "arpsid/core/c64_bus.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_sid_projection_bridge.h"
#include <cstdio>
#include <cstdlib>

using namespace ArpSID::C64;

static void require(bool v, const char* msg) {
    if (!v) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}

int main() {
    require(c64SidRegWriteable(0x00u), "$D400 writable");
    require(c64SidRegWriteable(0x18u), "$D418 MODE/VOL writable");
    for (uint8_t r = 0x19u; r <= 0x1Fu; ++r) {
        require(!c64SidRegWriteable(r), "$D419-$D41F not live-writeable");
    }

    C64Platform p{};
    p.reset(true);
    C64SidBridgeState bridge{};
    p.attachSid(&bridge);

    p.cpuWrite(0xD41Du, 0x1Du);
    p.cpuWrite(0xD41Eu, 0x1Eu);
    p.cpuWrite(0xD41Fu, 0x1Fu);
    require(bridge.writeCount == 0u, "$D41D-$D41F holes do not reach SID bridge");
    require(p.sidRegisterImage()[0x1Du] == 0u && p.sidRegisterImage()[0x1Eu] == 0u && p.sidRegisterImage()[0x1Fu] == 0u,
            "$D41D-$D41F holes do not mutate SID register image");

    require(!projectSidTimedWriteThroughC64Bus(p, 0x1Cu, 0x55u, 0u), "projection rejects ENV3 live write");
    require(!projectSidTimedWriteThroughC64Bus(p, 0x1Du, 0x55u, 0u), "projection rejects unmapped SID hole");
    require(projectSidTimedWriteThroughC64Bus(p, 0x18u, 0x0Fu, 0u), "projection accepts $D418");
    require(bridge.d418WriteCount == 1u, "accepted $D418 projection reaches bridge");

    const uint16_t bases[2] = {0xD400u, 0xD420u};
    require(p.configurePsidSidBases(bases, 2u), "configure primary+secondary SID bases");
    bridge.reset();
    p.cpuWrite(0xD438u, 0x22u); // exact secondary window: $D420 + $18
    require(bridge.writeCount == 1u && bridge.lastChip == 1u && bridge.lastReg == 0x18u,
            "$D438 hits secondary SID $18 when $D420 base is configured");
    bridge.reset();
    p.cpuWrite(0xD458u, 0x33u); // unassigned primary mirror
    require(bridge.writeCount == 1u && bridge.lastChip == 0u && bridge.lastReg == 0x18u,
            "unassigned $D458 falls back to primary SID $18 mirror under multi-SID");

    std::puts("c64_sid_unmapped_holes_v715_tests: PASS");
    return 0;
}
