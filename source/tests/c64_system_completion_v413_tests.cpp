// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_cia.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static void testRealBrkInterruptMode() {
    C64Platform p;
    p.reset(true);
    p.setTrapBrkAsJam(false);
    p.cpuWrite(0x0001, 0x35); // RAM visible at vectors, IO still visible.
    p.pokeMemory(0x0200, 0x00); // BRK
    p.pokeMemory(0x0201, 0xEA); // padding byte consumed by BRK semantics
    p.pokeMemory(0x1234, 0x40); // RTI
    p.pokeMemory(0xFFFE, 0x34);
    p.pokeMemory(0xFFFF, 0x12);
    p.cpu().state().pc = 0x0200;
    p.cpu().state().sp = 0xFF;
    p.cpu().state().p = Mos6510::U; // IRQ enabled, decimal off
    const uint8_t c = p.executeInstruction();
    require(c == 7, "BRK consumes 7 cycles");
    require(!p.cpu().state().jammed, "real BRK mode must not JAM");
    require(p.cpu().state().pc == 0x1234, "BRK vectors through IRQ vector");
    require((p.peekMemory(0x01FD) & Mos6510::B) != 0, "BRK pushes status with B flag set");
    const uint16_t pushedPc = uint16_t(p.peekMemory(0x01FE) | (uint16_t(p.peekMemory(0x01FF)) << 8));
    require(pushedPc == 0x0202, "BRK pushes PC+2");
    p.executeInstruction(); // RTI
    require(p.cpu().state().pc == 0x0202, "RTI restores BRK return PC");
}

static void testPsidBrkTrapCompatibility() {
    C64Platform p;
    p.reset(true);
    p.setTrapBrkAsJam(true);
    p.cpuWrite(0x0001, 0x35);
    p.pokeMemory(0x0200, 0x00);
    p.cpu().state().pc = 0x0200;
    p.cpu().state().p = Mos6510::U;
    p.executeInstruction();
    require(p.cpu().state().jammed, "PSID-fast BRK trap remains deterministic JAM");
}

static void testCiaPortDdrInputOutputMix() {
    Cia6526 cia;
    cia.reset();
    cia.write(0x02, 0xF0);      // high nibble output, low nibble input
    cia.write(0x00, 0xA5);      // output latch
    cia.setPortAInput(0x0C);    // external input low nibble
    require(cia.read(0x00) == 0xAC, "CIA PRA read mixes output latch with input pins by DDRA");

    cia.write(0x03, 0x0F);      // low nibble output, high nibble input
    cia.write(0x01, 0x56);
    cia.setPortBInput(0xA0);
    require((cia.read(0x01) & 0xBF) == 0xA6, "CIA PRB read mixes output latch with input pins by DDRB");
}

static void testDecimalArithmeticProof() {
    C64Platform p;
    p.reset(true);
    p.cpuWrite(0x0001, 0x35);
    // SED; CLC; LDA #$45; ADC #$55; STA $0300; SEC; SBC #$01; STA $0301; BRK
    const uint8_t prog[] = {0xF8,0x18,0xA9,0x45,0x69,0x55,0x8D,0x00,0x03,0x38,0xE9,0x01,0x8D,0x01,0x03,0x00};
    for (uint16_t i=0;i<sizeof(prog);++i) p.pokeMemory(uint16_t(0x0400+i), prog[i]);
    p.cpu().state().pc = 0x0400;
    p.cpu().state().p = Mos6510::U;
    p.setTrapBrkAsJam(true);
    for (int i=0;i<32 && !p.cpu().state().jammed;++i) p.executeInstruction();
    require(p.peekMemory(0x0300) == 0x00, "BCD ADC 45+55 -> 00 with carry");
    require(p.peekMemory(0x0301) == 0x99, "BCD SBC 00-01 with carry -> 99");
}

int main() {
    testRealBrkInterruptMode();
    testPsidBrkTrapCompatibility();
    testCiaPortDdrInputOutputMix();
    testDecimalArithmeticProof();
    std::cout << "C64SystemCompletionV413 runtime checks passed\n";
    return 0;
}
