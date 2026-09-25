#include "arpsid/core/c64_platform.h"
#include "arpsid/core/sid_measured_chip_variation.h"
#include <cstdio>
#include <cstdint>

using namespace ArpSID::C64;

static int failures = 0;
static void req(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; }
}
template<typename A, typename B>
static void eq(A a, B b, const char* msg) {
    if (a != b) {
        std::fprintf(stderr, "FAIL: %s got=%lld expected=%lld\n", msg,
                     static_cast<long long>(a), static_cast<long long>(b));
        ++failures;
    }
}

static void testCartridgePlaVisibility() {
    C64Platform p; p.reset();
    p.pokeMemory(0x8000, 0x11);
    p.cartridge().pokeRomL(0, 0x42);
    eq(p.cpuRead(0x8000), uint8_t{0x11}, "no cartridge exposes RAM at $8000");
    p.attachCartridge(C64CartridgeMode::EightK);
    eq(p.visibleDevice(0x8000), C64VisibleDevice::CartridgeLo, "8K cartridge maps ROML");
    eq(p.cpuRead(0x8000), uint8_t{0x42}, "8K cartridge ROML read");
    p.cartridge().pokeRomH(0, 0x99);
    p.attachCartridge(C64CartridgeMode::SixteenK);
    eq(p.visibleDevice(0xA000), C64VisibleDevice::CartridgeHi, "16K cartridge maps ROMH at $A000");
    eq(p.cpuRead(0xA000), uint8_t{0x99}, "16K cartridge ROMH read");
    p.attachCartridge(C64CartridgeMode::Ultimax);
    p.cartridge().pokeRomH(0, 0x77);
    eq(p.visibleDevice(0xE000), C64VisibleDevice::CartridgeHi, "Ultimax maps ROMH at $E000");
    eq(p.cpuRead(0xE000), uint8_t{0x77}, "Ultimax ROMH read");
}

static void runProgram(C64Platform& p, uint16_t pc, uint64_t maxInstructions = 32) {
    p.cpu().state().pc = pc;
    p.cpu().state().jammed = false;
    for (uint64_t i = 0; i < maxInstructions && !p.cpu().state().jammed; ++i) {
        p.executeInstruction();
    }
}

static void testIllegalOpcodeSubset() {
    C64Platform p; p.reset();
    p.pokeMemory(0x0010, 0x80);
    p.pokeMemory(0x0200, 0xA7); // LAX $10
    p.pokeMemory(0x0201, 0x10);
    p.pokeMemory(0x0202, 0x87); // SAX $11
    p.pokeMemory(0x0203, 0x11);
    p.pokeMemory(0x0204, 0x00); // BRK
    runProgram(p, 0x0200);
    eq(p.cpu().state().a, uint8_t{0x80}, "LAX loads A");
    eq(p.cpu().state().x, uint8_t{0x80}, "LAX loads X");
    eq(p.peekMemory(0x0011), uint8_t{0x80}, "SAX stores A&X");
    req((p.cpu().state().p & Mos6510::N) != 0, "LAX sets negative flag");

    C64Platform q; q.reset();
    q.pokeMemory(0x0012, 0x05);
    q.cpu().state().a = 0x05;
    q.pokeMemory(0x0300, 0xC7); // DCP $12 => mem=4, CMP A,mem => C set
    q.pokeMemory(0x0301, 0x12);
    q.pokeMemory(0x0302, 0x00);
    runProgram(q, 0x0300);
    eq(q.peekMemory(0x0012), uint8_t{0x04}, "DCP decrements memory");
    req((q.cpu().state().p & Mos6510::C) != 0, "DCP compares against A");
}

static void testVicSpriteDmaAndBadline() {
    VicII v; v.reset();
    v.write(0x11, 0x10); // display enabled, yscroll 0
    for (int i = 0; i < 0x30 * VicII::kPalCyclesPerLine + 16; ++i) v.tick();
    req(v.badline(), "VIC detects first display badline");
    req(!v.cpuCanUseBus(), "badline char fetch steals CPU bus");

    VicII s; s.reset();
    s.write(0x01, 0x00); // sprite 0 Y matches raster line 0
    s.write(0x15, 0x01); // enable sprite 0
    for (int i = 0; i < 58; ++i) s.tick();
    req(s.spriteDma(), "sprite DMA window active when sprite enabled");
    req(!s.cpuCanUseBus(), "sprite DMA steals CPU bus");
}

static void testCiaTodSerialAndIrqEdges() {
    Cia6526 cia; cia.reset();
    cia.write(0x0C, 0x00); // serial input register
    cia.setSpInput(1u);
    for (int i = 0; i < 8; ++i) {
        cia.setCntInput(0u);
        cia.setCntInput(1u);
        cia.step(0u);
    }
    req((cia.irqFlags() & 0x08u) != 0, "CIA serial completion sets IRQ flag");
    eq(cia.serialBitsRemaining(), uint8_t{0}, "CIA serial shifts exactly eight bits");
    eq(cia.serialShift(), uint8_t{0xFFu}, "CIA serial input samples SP on CNT edges");

    Cia6526 tod; tod.reset();
    for (int i = 0; i < 5; ++i) (void)tod.tick();
    eq(tod.todTenths(), uint8_t{1}, "CIA TOD tenth increments after deterministic 5-cycle divider");

    Cia6526 irq; irq.reset();
    irq.write(0x04, 0x01); irq.write(0x05, 0x00); // latch A = 1
    irq.write(0x0E, 0x11); // start + force load
    const bool e1 = irq.tick();
    const bool e2 = irq.tick();
    const bool e3 = irq.tick();
    req(!e1, "CIA timer latch=1 first tick only decrements");
    req(e2, "CIA timer IRQ is edge true when decrement wraps through $FFFF");
    req(!e3, "CIA timer IRQ is not level-returning after assertion");
}

static void testSidAnalogVariationProfiles() {
    const auto r2 = sidMeasuredProfileForRevision(SidDieRevision::MOS6581R2);
    const auto r5 = sidMeasuredProfileForRevision(SidDieRevision::MOS8580R5);
    req(r2.filterCutoffScale < r5.filterCutoffScale, "6581/8580 measured profile cutoff differs");
    req(r2.dacNonlinearity > r5.dacNonlinearity, "6581 profile has stronger DAC nonlinearity hook");
    req(r2.waveformDacIntegralNonlinearity[15] != r5.waveformDacIntegralNonlinearity[15], "profile exposes waveform INL table");
}

int main() {
    testCartridgePlaVisibility();
    testIllegalOpcodeSubset();
    testVicSpriteDmaAndBadline();
    testCiaTodSerialAndIrqEdges();
    testSidAnalogVariationProfiles();
    if (failures) return 1;
    std::puts("C64EmulatorScopeV274Tests PASS");
    return 0;
}
