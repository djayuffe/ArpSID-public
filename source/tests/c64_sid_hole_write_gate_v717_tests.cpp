// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_processor_port.h"
#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_psid_runtime.h"
#include <cstdlib>
#include <iostream>

using namespace ArpSID::C64;

struct CaptureSink final : ISidRegisterWriteSink {
    uint32_t writes = 0;
    uint8_t lastReg = 0;
    void writeSidRegisterPhi2(uint64_t, uint8_t reg, uint8_t, bool) noexcept override {
        ++writes;
        lastReg = reg;
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

    Phi2BusPhase phase{};
    mem.cpuWrite(100, 0xD418u, 0x0Fu, phase, false);
    require(sink.writes == 1u, "$D418 reaches sink");
    require(phase.sidWrite && phase.sidReg == 0x18u, "$D418 phase marked as SID write");

    for (uint16_t a = 0xD419u; a <= 0xD41Fu; ++a) {
        mem.cpuWrite(101 + (a - 0xD419u), a, static_cast<uint8_t>(a), phase, false);
        require(!phase.sidWrite, "$D419-$D41F must not be marked as live SID writes");
    }
    require(sink.writes == 1u, "$D419-$D41F do not reach MemoryMatrix SID sink");
    require(mem.droppedSidHoleWrites() == 7u, "MemoryMatrix counts dropped SID readback/hole writes");

    C64RuntimeSidSink local;
    C64RuntimePhi2SidSinkBridge bridge;
    bridge.attach(&local, nullptr);
    bridge.writeSidRegisterPhi2(200, 0x18u, 0x01u, false);
    bridge.writeSidRegisterPhi2(201, 0x1Cu, 0x02u, false);
    bridge.writeSidRegisterPhi2(202, 0x1Fu, 0x03u, false);
    require(local.writeCount == 1u, "runtime bridge forwards only writable SID register writes");
    require(local.droppedSidHoleWriteCount == 2u, "local sink counts dropped SID hole/readback writes");
    require(bridge.droppedSidHoleWriteCount() == 2u, "phi2 bridge counts dropped SID hole/readback writes");

    std::cout << "C64SidHoleWriteGateV717Tests PASS\n";
    return 0;
}
