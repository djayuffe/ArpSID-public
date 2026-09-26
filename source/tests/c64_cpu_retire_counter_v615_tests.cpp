// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_cpu6510_micro.h"
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
    Cpu6510Micro cpu;
    cpu.powerOn();

    // Manually start a NOP fetch/execute sequence at $1000. The exact bus data
    // contract is owned by Phi2Machine, but the micro CPU should retire exactly
    // once when finish_() completes the instruction.
    auto& s = cpu.state();
    s.pc = 0x1000;
    s.active = false;
    s.jammed = false;
    const auto r0 = cpu.retiredInstructionCount();

    // Fetch opcode.
    auto req = cpu.tickPhi2Begin();
    (void)req;
    cpu.tickPhi2End(0xEA); // NOP
    require(cpu.retiredInstructionCount() == r0, "fetch alone does not retire NOP");

    // Execute NOP final cycle.
    req = cpu.tickPhi2Begin();
    (void)req;
    cpu.tickPhi2End(0xEA);
    require(cpu.retiredInstructionCount() == r0 + 1u, "NOP increments retire counter exactly once");

    cpu.powerOn();
    require(cpu.retiredInstructionCount() == 0u, "powerOn clears retire counter");

    std::cout << "C64 CPU retire counter regression passed\n";
    return 0;
}
