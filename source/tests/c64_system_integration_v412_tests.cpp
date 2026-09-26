// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static void testEffective6510PortAndPlaVisibility() {
    C64Platform p;
    p.reset(true);

    p.cpuWrite(0x0000, 0x00); // all processor port bits input => pulled high on C64 bus
    p.cpuWrite(0x0001, 0x00);
    require(p.effectiveProcessorPort() == 0xFF, "effective $01 must respect DDR input pull-ups");
    require(p.cpuRead(0x0001) == 0xFF, "reading $0001 must return effective processor port, not raw latch");
    require(p.visibleDevice(0xD000) == C64VisibleDevice::Io, "effective CHAREN high should expose IO when DDR bits are inputs");

    p.cpuWrite(0x0000, 0x07);
    p.cpuWrite(0x0001, 0x00); // LORAM/HIRAM/CHAREN low => RAM through $D000-$FFFF
    require((p.effectiveProcessorPort() & 0x07u) == 0x00u, "processor port output bits should be driven by latch");
    require(p.visibleDevice(0xD400) == C64VisibleDevice::Ram, "LORAM/HIRAM low should map RAM instead of SID IO");
}

static void testColorRamDecode() {
    C64Platform p;
    p.reset(true);
    p.cpuWrite(0xD800, 0x2E);
    require(p.cpuRead(0xD800) == 0x2E, "color RAM reads must expose low nibble with high nibble from live open bus");
    require((p.colorRam()[0] & 0x0F) == 0x0E, "color RAM storage must be 4-bit");

    p.cpuWrite(0xDBFF, 0x05);
    require(p.cpuRead(0xDBFF) == 0x05, "color RAM end address decode must preserve live open-bus high nibble");
}

static void testCpuIrqAndNmiService() {
    C64Platform p;
    p.reset(true);
    // Map RAM at vectors so the test owns the interrupt vectors without depending on ROM contents.
    p.cpuWrite(0x0000, 0x07);
    p.cpuWrite(0x0001, 0x35); // HIRAM low; IO still visible through LORAM/CHAREN

    p.pokeMemory(0xFFFE, 0x00); p.pokeMemory(0xFFFF, 0x09);
    p.pokeMemory(0xFFFA, 0x00); p.pokeMemory(0xFFFB, 0x0A);
    p.pokeMemory(0x0900, 0x40); // RTI
    p.pokeMemory(0x0A00, 0x40); // RTI

    p.cpu().state().pc = 0x0800;
    p.cpu().state().p = uint8_t(Mos6510::U); // IRQ enabled (I clear)
    p.cpu().irq(true);
    p.executeInstruction();
    require(p.cpu().state().pc == 0x0900, "IRQ service should vector through $FFFE/$FFFF before next opcode");
    require((p.cpu().state().p & Mos6510::I) != 0, "IRQ service should set I flag");

    p.cpu().state().pc = 0x0810;
    p.cpu().state().p = uint8_t(Mos6510::U | Mos6510::I); // NMI ignores I
    p.cpu().nmi(true);
    p.executeInstruction();
    require(p.cpu().state().pc == 0x0A00, "NMI rising edge should vector through $FFFA/$FFFB");
    require(!p.cpu().nmiPending(), "NMI pending latch should clear after service");
}

static void testPlatformInterruptLinesFromChips() {
    C64Platform p;
    p.reset(true);
    p.cia1().write(0x04, 0x00); p.cia1().write(0x05, 0x00);
    p.cia1().write(0x0D, 0x81); // enable Timer A IRQ
    p.cia1().write(0x0E, 0x11); // start + force load
    p.runCycles(2);
    require(p.irqLine(), "CIA1 IRQ should be wired to CPU IRQ line");

    p.reset(true);
    p.cia2().write(0x04, 0x00); p.cia2().write(0x05, 0x00);
    p.cia2().write(0x0D, 0x81);
    p.cia2().write(0x0E, 0x11);
    p.runCycles(2);
    require(p.nmiLine(), "CIA2 IRQ should be wired to CPU NMI line");

    p.reset(true);
    p.cpuWrite(0xD012, 0x02);
    p.cpuWrite(0xD01A, 0x01);
    p.runCycles(2u * 63u + 1u);
    require(p.irqLine(), "VIC raster IRQ should be wired to CPU IRQ line");
}

static void testRsidBootstrapContract() {
    C64Platform p;
    p.reset(true);
    // init routine: STA $D402; RTS. Bootstrap must call with A=song-1.
    p.pokeMemory(0x2000, 0x8D); p.pokeMemory(0x2001, 0x02); p.pokeMemory(0x2002, 0xD4); p.pokeMemory(0x2003, 0x60);
    const uint16_t entry = p.installRsidBootstrap(0x2000, 3, 0x0800);
    require(entry == 0x0800, "RSID bootstrap should return requested entry");
    p.bootFromResetVector();
    p.startRealtimeSidCore();
    (void)p.runRealtimeSidCoreCycles(128, 32);
    require(p.sidRegisterImage()[0x02] == 0x02, "RSID bootstrap should pass songNumber-1 in A to init routine");
    require(p.cpu().state().jammed, "RSID bootstrap should jump to deterministic $FFFF sentinel after init");
}

int main() {
    testEffective6510PortAndPlaVisibility();
    testColorRamDecode();
    testCpuIrqAndNmiService();
    testPlatformInterruptLinesFromChips();
    testRsidBootstrapContract();
    std::cout << "C64 system integration v412 tests passed\n";
    return 0;
}
