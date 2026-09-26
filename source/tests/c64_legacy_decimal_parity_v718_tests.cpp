// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_6510.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

struct RamBus final : Mos6510Bus {
    uint8_t mem[65536]{};
    uint8_t cpuRead(uint16_t a) noexcept override { return mem[a]; }
    void cpuWrite(uint16_t a, uint8_t v) noexcept override { mem[a] = v; }
};

static void require(bool v, const char* msg) {
    if (!v) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static Mos6510State runAdc(uint8_t a, uint8_t operand, bool carry) {
    RamBus bus;
    bus.mem[0x0200] = 0xF8; // SED
    bus.mem[0x0201] = 0x69; // ADC #imm
    bus.mem[0x0202] = operand;
    bus.mem[0x0203] = 0x00; // BRK/JAM in this legacy harness
    Mos6510 cpu;
    cpu.reset(0x0200);
    cpu.state().a = a;
    if (carry) cpu.state().p |= Mos6510::C; else cpu.state().p &= uint8_t(~Mos6510::C);
    cpu.tickWithOpcodeHint(bus, 0xF8);
    cpu.tickWithOpcodeHint(bus, 0x69);
    return cpu.state();
}

static Mos6510State runSbc(uint8_t a, uint8_t operand, bool carry) {
    RamBus bus;
    bus.mem[0x0200] = 0xF8; // SED
    bus.mem[0x0201] = 0xE9; // SBC #imm
    bus.mem[0x0202] = operand;
    bus.mem[0x0203] = 0x00;
    Mos6510 cpu;
    cpu.reset(0x0200);
    cpu.state().a = a;
    if (carry) cpu.state().p |= Mos6510::C; else cpu.state().p &= uint8_t(~Mos6510::C);
    cpu.tickWithOpcodeHint(bus, 0xF8);
    cpu.tickWithOpcodeHint(bus, 0xE9);
    return cpu.state();
}

int main() {
    auto adcZero = runAdc(0x00, 0x00, false);
    require(adcZero.a == 0x00, "decimal ADC 00+00 result");
    require((adcZero.p & Mos6510::Z) != 0, "legacy decimal ADC sets Z from adjusted final result");
    require((adcZero.p & Mos6510::N) == 0, "legacy decimal ADC sets N from adjusted final result");

    auto adcNeg = runAdc(0x49, 0x50, false); // adjusted result 0x99
    require(adcNeg.a == 0x99, "decimal ADC adjusted 49+50 -> 99");
    require((adcNeg.p & Mos6510::N) != 0, "legacy decimal ADC sets N from adjusted final result");

    auto sbcZero = runSbc(0x00, 0x00, true);
    require(sbcZero.a == 0x00, "decimal SBC 00-00 result");
    require((sbcZero.p & Mos6510::Z) != 0, "legacy decimal SBC sets Z from adjusted final result");
    require((sbcZero.p & Mos6510::N) == 0, "legacy decimal SBC sets N from adjusted final result");

    std::cout << "C64LegacyDecimalParityV718Tests PASS\n";
    return 0;
}
