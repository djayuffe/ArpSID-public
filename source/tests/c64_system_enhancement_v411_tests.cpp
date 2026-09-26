// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_vic.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static void testCiaTimersAndIcr() {
    Cia6526 cia;
    cia.reset();
    cia.setClockHz(kPalPhi2Hz);
    cia.setTod50Hz(true);

    // Timer A latch=1, IRQ enabled, continuous mode.
    cia.write(0x04, 0x01);
    cia.write(0x05, 0x00);
    cia.write(0x0D, 0x81); // enable TA IRQ
    cia.write(0x0E, 0x11); // start + force load
    cia.step(2);
    require(cia.irq(), "CIA Timer A should assert IRQ after underflow");
    const uint8_t icr = cia.read(0x0D);
    require((icr & 0x81) == 0x81, "CIA ICR should report bit7 + Timer A");
    require(!cia.irq(), "CIA ICR read should clear IRQ line");

    // Timer B chained from Timer A underflow.
    cia.reset();
    cia.write(0x04, 0x00); cia.write(0x05, 0x00); // TA underflows quickly after force-load zero
    cia.write(0x06, 0x00); cia.write(0x07, 0x00);
    cia.write(0x0D, 0x82); // enable TB IRQ
    cia.write(0x0F, 0x41); // TB counts Timer A underflows, start
    cia.write(0x0E, 0x11); // TA start + force load
    cia.step(3);
    require((cia.irqFlags() & Cia6526::IcrTimerA) != 0, "Timer A underflow should be latched");
}

static void testCiaTodLatchSerialAndPb() {
    Cia6526 cia;
    cia.reset();
    cia.setClockHz(kPalPhi2Hz);
    cia.setTod50Hz(true);

    cia.write(0x0B, 0x01); // hours stops TOD during the write sequence
    cia.write(0x0A, 0x00);
    cia.write(0x09, 0x00);
    cia.write(0x08, 0x00); // tenths restarts TOD, 01:00:00.0
    cia.step((kPalPhi2Hz / 10u) + 64u);
    require(cia.todTenths() == 0x01, "CIA TOD should advance one tenth at PAL 50Hz mode");

    // TOD read latch: hours read latches, tenths releases.
    const uint8_t hr = cia.read(0x0B);
    const uint8_t sec = cia.read(0x09);
    const uint8_t tenth = cia.read(0x08);
    require(hr == 0x01 && sec == 0x00 && tenth == 0x01, "CIA TOD latch/read-release semantics broken");

    // PB6 pulse/toggle and serial output are driven by Timer A underflows.
    cia.reset();
    cia.write(0x0C, 0x80); // serial byte, MSB first
    cia.write(0x04, 0x00); cia.write(0x05, 0x00);
    cia.write(0x0E, 0x43); // start, PB6 output enable, serial output mode
    cia.step(1);
    require(cia.pb6() == 1, "CIA PB6 should toggle on Timer A underflow");
    require(cia.spOut() == 1, "CIA serial output should shift MSB on Timer A underflow/2 phase");
}

static void testVicPalNtscIrqAndContention() {
    VicII vic;
    vic.reset(true);
    require(vic.cyclesPerLine() == 63 && vic.rasterLines() == 312, "PAL VIC geometry wrong");
    vic.reset(false);
    require(vic.cyclesPerLine() == 65 && vic.rasterLines() == 263, "NTSC VIC geometry wrong");

    vic.reset(true);
    vic.write(0x11, 0x10); // display enabled, yscroll=0
    vic.step(0x30 * 63u + 1u); // line $30
    require(vic.badline(), "VIC badline should assert on display line with matching yscroll");
    const uint32_t stolen = vic.previewStolen(63);
    require(stolen >= 40, "VIC badline preview should show character-fetch stolen cycles");

    vic.reset(true);
    vic.write(0x12, 0x02);
    vic.write(0x1A, 0x01);
    vic.step(2 * 63u + 1u);
    require(vic.irq(), "VIC raster IRQ should assert when enabled and compare line reached");
    vic.write(0x19, 0x01);
    require(!vic.irq(), "VIC raster IRQ acknowledge should clear IRQ status");
}

static void testPlatformWiresCiaVic() {
    C64Platform platform;
    platform.reset(false);
    require(platform.clockHz() == kNtscPhi2Hz, "platform NTSC clock not selected");
    require(platform.vic().cyclesPerLine() == 65, "platform did not reset VIC as NTSC");
    require((platform.cia1().read(0x0E) & 0x80u) == 0, "NTSC CIA TOD should default to 60Hz mode");

    platform.reset(true);
    require(platform.clockHz() == kPalPhi2Hz, "platform PAL clock not selected");
    require(platform.vic().cyclesPerLine() == 63, "platform did not reset VIC as PAL");
    require((platform.cia1().read(0x0E) & 0x80u) != 0, "PAL CIA TOD should default to 50Hz mode");

    platform.cpuWrite(0xD011, 0x10);
    platform.runCycles(0x31u * 63u);
    require(platform.vic().totalStolen() > 0, "platform should accumulate VIC stolen cycles through runCycles");
}

int main() {
    testCiaTimersAndIcr();
    testCiaTodLatchSerialAndPb();
    testVicPalNtscIrqAndContention();
    testPlatformWiresCiaVic();
    std::cout << "C64 system enhancement v411 tests passed\n";
    return 0;
}
