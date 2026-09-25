#include "arpsid/core/c64_platform.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ArpSID::C64;

static int fails = 0;
static void req(bool c, const char* m) { if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++fails; } }

struct Sink final : SidRegisterSink {
    uint8_t regs[32]{};
    uint8_t lastReg = 0xff;
    uint8_t lastValue = 0;
    uint64_t lastCycle = 0;
    uint32_t writes = 0;
    void sidWrite(uint8_t r, uint8_t v, uint64_t c) noexcept override {
        r &= 0x1f; regs[r] = v; lastReg = r; lastValue = v; lastCycle = c; ++writes;
    }
    uint8_t sidRead(uint8_t r, uint64_t) noexcept override { return regs[r & 0x1f]; }
};

static void installBootSidWriter(C64Platform& p, uint8_t value) {
    // The HLE KERNAL reset vector points to $E000. Put a tiny deterministic boot
    // loop there: LDA #value ; STA $D400 ; JMP $E000.
    p.roms().pokeKernal(0xE000u, 0xA9u);
    p.roms().pokeKernal(0xE001u, value);
    p.roms().pokeKernal(0xE002u, 0x8Du);
    p.roms().pokeKernal(0xE003u, 0x00u);
    p.roms().pokeKernal(0xE004u, 0xD4u);
    p.roms().pokeKernal(0xE005u, 0x4Cu);
    p.roms().pokeKernal(0xE006u, 0x00u);
    p.roms().pokeKernal(0xE007u, 0xE0u);
}

static void testBootStartsFromResetVectorAndWritesSid() {
    C64Platform p;
    Sink s;
    p.reset(true);
    p.attachSid(&s);
    installBootSidWriter(p, 0x5au);

    p.bootFromResetVector();
    req(p.booted(), "booted flag set");
    req(p.cpu().state().pc == 0xE000u, "PC loaded from visible HLE KERNAL reset vector");
    p.startRealtimeSidCore();
    req(p.realtimeSidCoreRunning(), "realtime SID-core CPU running flag set");

    (void)p.runRealtimeSidCoreCycles(64u, 32u, &s);
    req(p.phi2Cycle() == 64u, "runtime advances exact requested PHI2 budget");
    req(s.writes >= 1u, "boot code wrote at least one SID register");
    req(s.regs[0] == 0x5au, "boot code writes through real $D400 bus path");
    req(p.sidRegisterImage()[0] == 0x5au, "C64Platform SID image is updated by realtime boot CPU");
}

static void testBoundedCpuStillAdvancesCiaVicBus() {
    C64Platform p;
    p.reset(true);
    installBootSidWriter(p, 0x33u);
    p.bootFromResetVector();
    p.startRealtimeSidCore();

    const uint64_t before = p.phi2Cycle();
    (void)p.runRealtimeSidCoreCycles(1024u, 1u, nullptr);
    req(p.phi2Cycle() == before + 1024u, "bounded CPU mode still advances full PHI2 time");
    req(p.vic().cycleInLine() != 0u || p.vic().rasterLine() != 0u, "VIC mirror advances during bounded realtime run");
}

static void testStopRealtimeLeavesPassiveMirrorClocked() {
    C64Platform p;
    p.reset(true);
    p.bootFromResetVector();
    p.startRealtimeSidCore();
    p.stopRealtimeSidCore();
    const uint16_t pc = p.cpu().state().pc;
    (void)p.runRealtimeSidCoreCycles(128u, 64u, nullptr);
    req(p.phi2Cycle() == 128u, "stopped realtime core still advances passive platform cycles");
    req(p.cpu().state().pc == pc, "stopped realtime core does not execute CPU opcodes");
}

int main() {
    testBootStartsFromResetVectorAndWritesSid();
    testBoundedCpuStillAdvancesCiaVicBus();
    testStopRealtimeLeavesPassiveMirrorClocked();
    if (fails) return 1;
    std::puts("C64SidcoreRealtimeBootV275Tests PASS");
    return 0;
}
