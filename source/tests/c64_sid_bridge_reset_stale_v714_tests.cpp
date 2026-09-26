// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_sid_bridge.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool v, const char* msg) {
    if (!v) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    C64SidBridgeState b;
    b.sidWrite(0x18u, 0x0Au, 100u);
    b.sidWrite(0x18u, 0x0Au, 101u);
    require(b.timedWriteCount == 2u, "precondition timed writes captured");
    require(b.d418RepeatedValueWriteCount == 1u, "precondition repeated D418 counted");
    b.reset();
    require(b.timedWriteCount == 0u, "reset clears timed count");
    require(b.timedWriteOverflow == 0u, "reset clears overflow");
    require(b.writeCount == 0u && b.d418WriteCount == 0u, "reset clears write counters");
    require(!b.d418Observed && b.lastD418Value == 0u && b.lastD418Cycle == 0u, "reset clears D418 state");
    require(b.timedWrites[0].phi2Cycle == 0u && b.timedWrites[0].value == 0u && b.timedWrites[0].reg == 0u, "reset clears stale timed write entry 0");
    require(b.timedWrites[1].phi2Cycle == 0u && b.timedWrites[1].value == 0u && b.timedWrites[1].reg == 0u, "reset clears stale timed write entry 1");
    std::cout << "C64SidBridgeResetStaleV714Tests PASS\n";
    return 0;
}
