// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_processor_port.h"
#include "arpsid/core/c64_open_bus.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

struct CaptureSink final : ISidRegisterWriteSink {
    struct W { uint64_t phi2; uint8_t reg; uint8_t value; bool rmw; };
    W writes[16]{};
    size_t count = 0;
    void writeSidRegisterPhi2(uint64_t phi2, uint8_t reg, uint8_t value, bool rmwDummy) noexcept override {
        if (count < 16) writes[count++] = W{phi2, reg, value, rmwDummy};
    }
};

static void require(bool v, const char* msg) {
    if (!v) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    ProcessorPort6510 port;
    OpenBusLatch bus;
    MemoryMatrix mem;
    CaptureSink sink;
    port.powerOn();
    bus.powerOn();
    mem.attach(&port, &bus, &sink);
    mem.powerOn(true);

    const uint16_t bases[] = {0xD400u, 0xD420u, 0xD500u};
    mem.configureSidBases(bases, 3);

    Phi2BusPhase phase{};
    mem.cpuWrite(10, 0xD418u, 0x01u, phase, false);
    require(phase.sidWrite && phase.sidReg == 0x18u, "$D418 writes primary MODE/VOL");
    mem.cpuWrite(11, 0xD438u, 0x02u, phase, false);
    require(phase.sidWrite && phase.sidReg == 0x20u + 0x18u, "$D438 exact secondary window writes chip1 MODE/VOL");
    mem.cpuWrite(12, 0xD458u, 0x03u, phase, false);
    require(phase.sidWrite && phase.sidReg == 0x18u, "$D458 falls back to physical primary SID mirror under multi-SID");
    mem.cpuWrite(13, 0xD518u, 0x04u, phase, false);
    require(phase.sidWrite && phase.sidReg == 0x40u + 0x18u, "$D518 exact third SID window writes chip2 MODE/VOL");
    mem.cpuWrite(14, 0xD598u, 0x05u, phase, false);
    require(phase.sidWrite && phase.sidReg == 0x18u, "$D598 unassigned mirror still reaches primary SID MODE/VOL");

    require(sink.count == 5u, "all SID writes reached sink");
    require(sink.writes[0].reg == 0x18u, "first write chip0/reg18");
    require(sink.writes[1].reg == 0x20u + 0x18u, "second write chip1/reg18 encoded");
    require(sink.writes[2].reg == 0x18u, "third write primary mirror chip0/reg18");
    require(sink.writes[3].reg == 0x40u + 0x18u, "fourth write chip2/reg18 encoded");
    require(sink.writes[4].reg == 0x18u, "fifth write primary mirror chip0/reg18");
    require(sink.writes[0].phi2 == 10u && sink.writes[4].phi2 == 14u, "absolute phi2 timestamps preserved");

    std::cout << "C64SidMirrorMultiSidV713Tests PASS\n";
    return 0;
}
