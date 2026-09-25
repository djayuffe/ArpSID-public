#include "arpsid/core/c64_vic.h"

#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;

    VicII vic;
    vic.reset(true);
    vic.write(0x1A, 0x01); // enable raster IRQ

    // Program 9-bit compare $1FF via $D011 bit7 + $D012=$FF.
    // PAL has only 312 lines (0..311), so $1FF must never match. A broken
    // modulo implementation fires at 511 % 312 = 199.
    vic.write(0x11, 0x80);
    vic.write(0x12, 0xFF);
    const uint32_t onePalFrame = VicII::kPalCyclesPerLine * VicII::kPalRasterLines;
    for (uint32_t i = 0; i < onePalFrame + VicII::kPalCyclesPerLine; ++i) vic.tick();
    require((vic.read(0x19) & 0x81u) == 0x00u,
            "out-of-range 9-bit raster compare $1FF must not modulo-fire on PAL");
    require(!vic.irq(), "out-of-range raster compare must not assert VIC IRQ");

    // Now program an in-range 9-bit compare, line $100, and verify it fires.
    vic.write(0x19, 0x01); // ACK any accidental status defensively
    vic.write(0x11, 0x80);
    vic.write(0x12, 0x00);
    for (uint32_t i = 0; i < onePalFrame; ++i) {
        if ((vic.read(0x19) & 0x81u) == 0x81u) break;
        vic.tick();
    }
    require((vic.read(0x19) & 0x81u) == 0x81u,
            "in-range 9-bit raster compare $100 fires and sets master IRQ bit");
    vic.write(0x19, 0x01);
    require(!vic.irq(), "$D019 write-one ACK clears in-range raster IRQ");

    std::cout << "c64_vic_raster_compare_v716_tests PASS\n";
    return 0;
}
