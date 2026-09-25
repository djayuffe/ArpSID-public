#include "arpsid/core/c64_platform.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

static void req(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

struct Sink final : SidRegisterSink {
    std::vector<BusCycle> writes;
    std::vector<BusCycle> reads;
    uint8_t sidRead(uint8_t reg, uint64_t phi2Cycle) noexcept override {
        reads.push_back(BusCycle{uint16_t(0xD400u | reg), uint8_t(0x80u | reg), true, phi2Cycle, 0});
        return uint8_t(0x80u | reg);
    }
    void sidWrite(uint8_t reg, uint8_t value, uint64_t phi2Cycle) noexcept override {
        writes.push_back(BusCycle{uint16_t(0xD400u | reg), value, false, phi2Cycle, 0});
    }
};

int main() {
    // Same-PHI2 order is deterministic, write-before-read, then insertion order.
    C64Platform c64;
    c64.reset(true);
    Sink sink;
    c64.attachSid(&sink);
    req(c64.scheduleCpuRead(0xD41B, 2), "schedule same-cycle SID read");
    req(c64.scheduleCpuWrite(0xD400, 0x11, 2), "schedule same-cycle SID write 1");
    req(c64.scheduleCpuWrite(0xD401, 0x22, 2), "schedule same-cycle SID write 2");
    c64.runCycles(3);
    req(sink.writes.size() == 2, "two writes before read were dispatched");
    req(sink.writes[0].phi2Cycle == 2 && (sink.writes[0].address & 0x1F) == 0x00, "same PHI2 first write stable");
    req(sink.writes[1].phi2Cycle == 2 && (sink.writes[1].address & 0x1F) == 0x01, "same PHI2 second write stable");
    req(!sink.reads.empty() && sink.reads[0].phi2Cycle == 2, "same PHI2 read dispatched after writes");
    req(c64.readOpenBus() == 0x9B, "SID read drives open bus with sink value");

    // Minimal 6510 opcode adapter can execute a tiny SID init program with cycle accounting.
    C64Platform p;
    p.reset(true);
    Sink psink;
    p.attachSid(&psink);
    p.pokeMemory(0x0801, 0xA9); // LDA #$0f
    p.pokeMemory(0x0802, 0x0F);
    p.pokeMemory(0x0803, 0x8D); // STA $D418
    p.pokeMemory(0x0804, 0x18);
    p.pokeMemory(0x0805, 0xD4);
    p.pokeMemory(0x0806, 0x00); // BRK/JAM stop
    p.cpu().state().pc = 0x0801;
    req(p.executeInstruction() == 2, "6510 LDA immediate cycles");
    req(p.executeInstruction() == 4, "6510 STA absolute cycles");
    req(psink.writes.size() == 1 && (psink.writes[0].address & 0x1F) == 0x18 && psink.writes[0].data == 0x0F, "6510 STA reaches SID sink");
    req(p.sidRegisterImage()[0x18] == 0x0F, "6510 STA updates SID register image");

    // CIA latch/mask/ICR behavior is deterministic enough for SID driver timing tests.
    Cia6526 cia;
    cia.reset();
    cia.write(0x04, 2);
    cia.write(0x05, 0);
    cia.write(0x0D, 0x81); // enable timer A IRQ
    cia.write(0x0E, 0x11); // force-load + start
    cia.tick();
    req(!cia.irq(), "CIA no IRQ before timer A underflow");
    cia.tick();
    req(!cia.irq(), "CIA latch=2 reaches zero before underflow");
    cia.tick();
    req(cia.irq(), "CIA masked timer A IRQ at underflow");
    const uint8_t icr = cia.read(0x0D);
    req((icr & 0x81u) == 0x81u && !cia.irq(), "CIA ICR read reports and clears IRQ");

    // VIC badline bus-steal skeleton exposes BA/AEC state at deterministic raster/cycle positions.
    C64Platform v;
    v.reset(true);
    v.scheduleCpuWrite(0xD011, 0x10, 0); // display enabled, yscroll 0
    v.runCycles(uint64_t(0x30) * VicII::kPalCyclesPerLine + 20);
    req(v.vic().rasterLine() == 0x30, "VIC reaches first PAL badline candidate");
    req(v.vic().badline(), "VIC badline active with display enabled");
    req(!v.vic().cpuCanUseBus(), "VIC badline character-fetch steals CPU bus in fetch window");

    std::cout << "C64PlatformCompletionV256Tests PASS\n";
    return 0;
}
