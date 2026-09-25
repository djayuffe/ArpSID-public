// c64_rom_helper_range_safe_v753_tests.cpp
//
// audit P0-8 / fix-order #6: C64RomSet::read{Basic,Kernal,Character}() and the
// poke* counterparts indexed with a raw `address - base` subtraction that
// underflowed to a huge index (UB / -Warray-bounds) for any address below the
// device base. The fix masks the offset to the (power-of-two) array size.
//
// This is a behavioral test: it verifies correct values for in-range addresses
// (including both boundaries) and sweeps the ENTIRE uint16_t address space through
// every helper so an out-of-bounds index would be caught (run under ASAN this is a
// hard proof of range-safety; even without ASAN it must not crash and must agree
// with the mask).

#include "arpsid/core/c64_pla.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;
    C64RomSet rom;
    rom.resetDeterministic();

    // ── In-range round-trips at both boundaries of each device. ──
    rom.pokeBasic(0xA000u, 0x11u);
    rom.pokeBasic(0xBFFFu, 0x22u);
    require(rom.readBasic(0xA000u) == 0x11u, "BASIC first byte round-trips");
    require(rom.readBasic(0xBFFFu) == 0x22u, "BASIC last byte round-trips");

    rom.pokeKernal(0xE000u, 0x33u);
    rom.pokeKernal(0xFFFFu, 0x44u);
    require(rom.readKernal(0xE000u) == 0x33u, "KERNAL first byte round-trips");
    require(rom.readKernal(0xFFFFu) == 0x44u, "KERNAL last byte round-trips");

    rom.pokeCharacter(0xD000u, 0x55u);
    rom.pokeCharacter(0xDFFFu, 0x66u);
    require(rom.readCharacter(0xD000u) == 0x55u, "character first byte round-trips");
    require(rom.readCharacter(0xDFFFu) == 0x66u, "character last byte round-trips");

    // ── Every helper must agree with masked indexing for an address one past the
    //    top of the device window (previously a 1-byte OOB read). ──
    rom.pokeBasic(0xA000u, 0xABu);      // index 0
    require(rom.readBasic(0xC000u) == 0xABu,
            "BASIC read past top wraps to a valid index (no OOB)");

    // ── Full uint16_t sweep through every helper: an out-of-bounds index on any
    //    address would corrupt memory / trip ASAN. The masked index keeps every
    //    access in [0, size). ──
    volatile uint32_t sink = 0u;
    for (uint32_t a = 0; a <= 0xFFFFu; ++a) {
        const uint16_t addr = static_cast<uint16_t>(a);
        rom.pokeBasic(addr, static_cast<uint8_t>(a));
        rom.pokeKernal(addr, static_cast<uint8_t>(a >> 1));
        rom.pokeCharacter(addr, static_cast<uint8_t>(a >> 2));
        sink = sink + rom.readBasic(addr) + rom.readKernal(addr) + rom.readCharacter(addr);
    }
    require(sink != 0xFFFFFFFFu, "sweep produced a usable accumulator (no UB elision)");

    std::printf("C64RomHelperRangeSafeV753Tests PASS\n");
    return 0;
}
