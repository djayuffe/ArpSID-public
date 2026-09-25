#include "arpsid/core/c64_platform.h"
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

static void req(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

struct Sink final : SidRegisterSink {
    std::vector<BusCycle> writes;
    uint8_t sidRead(uint8_t reg, uint64_t phi2Cycle) noexcept override {
        (void)phi2Cycle;
        return uint8_t(0xA0u | (reg & 0x1Fu));
    }
    void sidWrite(uint8_t reg, uint8_t value, uint64_t phi2Cycle) noexcept override {
        writes.push_back(BusCycle{uint16_t(0xD400u | reg), value, false, phi2Cycle});
    }
};

int main() {
    C64Platform c64;
    c64.reset(true);
    req(c64.clockHz() == kPalPhi2Hz, "PAL clock authority");
    Sink sink;

    req(c64.scheduleCpuWrite(0xD418, 0x0F, 3), "schedule SID volume write");
    req(c64.scheduleCpuWrite(0xD400, 0x34, 1), "schedule SID freq lo write");
    req(c64.scheduleCpuWrite(0xD401, 0x12, 2), "schedule SID freq hi write");
    c64.runCycles(4, &sink);

    req(sink.writes.size() == 3, "three SID writes dispatched");
    req(sink.writes[0].phi2Cycle == 1 && (sink.writes[0].address & 0x1F) == 0x00 && sink.writes[0].data == 0x34, "write 0 exact PHI2 order");
    req(sink.writes[1].phi2Cycle == 2 && (sink.writes[1].address & 0x1F) == 0x01 && sink.writes[1].data == 0x12, "write 1 exact PHI2 order");
    req(sink.writes[2].phi2Cycle == 3 && (sink.writes[2].address & 0x1F) == 0x18 && sink.writes[2].data == 0x0F, "write 2 exact PHI2 order");
    req(c64.sidRegisterImage()[0] == 0x34 && c64.sidRegisterImage()[1] == 0x12 && c64.sidRegisterImage()[0x18] == 0x0F, "SID image updated");

    c64.scheduleCpuWrite(0xD011, 0x1B, 0);
    c64.runCycles(64, nullptr);
    req(c64.vic().rasterLine() >= 1, "VIC raster advances from PHI2 cycles");

    c64.cia1().write(0x04, 2);
    c64.cia1().write(0x05, 0);
    c64.cia1().write(0x0D, 0x81);
    c64.cia1().write(0x0E, 1);
    c64.runCycles(3, nullptr);
    req(c64.cia1().irq(), "CIA timer IRQ after cycle ticks");

    c64.scheduleCpuRead(0xD41B, 0);
    c64.runCycles(1, &sink);
    req(c64.readOpenBus() == 0xBB, "SID read updates open bus through sink");

    std::cout << "C64PlatformSidCoreV255Tests PASS\n";
    return 0;
}
