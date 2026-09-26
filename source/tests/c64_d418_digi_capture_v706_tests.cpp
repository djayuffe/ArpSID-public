// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_d418_capture.h"
#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_sid_bus_sink.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;

    C64SidBridgeState bridge;
    bridge.sidWrite(0x18u, 0x0Fu, 100u);
    bridge.sidWrite(0x18u, 0x0Fu, 200u); // repeated identical sample must survive
    bridge.sidWrite(0x18u, 0x00u, 300u);
    bridge.sidWrite(0x04u, 0x11u, 400u);

    require(bridge.writeCount == 4u, "bridge records all SID writes");
    require(bridge.d418WriteCount == 3u, "$D418 counter includes repeated values");
    require(bridge.d418RepeatedValueWriteCount == 1u, "$D418 repeated-value counter increments");
    require(bridge.lastD418Cycle == 300u, "$D418 last absolute PHI2 cycle stored");

    D418CaptureSample samples[8]{};
    const size_t n = extractD418Samples(bridge, samples, 8, kPalPhi2Hz, 48000u);
    require(n == 3u, "extractD418Samples keeps every $D418 write");
    require(samples[0].phi2 == 100u && samples[1].phi2 == 200u && samples[2].phi2 == 300u,
            "$D418 PHI2 timestamps are absolute and monotonic");
    require(samples[0].nibble == 0x0Fu && samples[1].nibble == 0x0Fu && samples[2].nibble == 0x00u,
            "$D418 low nibble is the PCM sample");
    require(std::fabs(samples[0].unit - 1.0f) < 0.0001f, "$D418 nibble 15 maps to +1");
    require(std::fabs(samples[2].unit + 1.0f) < 0.0001f, "$D418 nibble 0 maps to -1");

    float zoh[32]{};
    C64SidBridgeState fast;
    fast.sidWrite(0x18u, 0x00u, 0u);
    fast.sidWrite(0x18u, 0x0Fu, 10u);
    const size_t applied = reconstructD418Zoh(fast, zoh, 32, 1000u, 1000u);
    require(applied == 2u, "ZOH applies both $D418 writes");
    require(zoh[0] < -0.99f && zoh[9] < -0.99f && zoh[10] > 0.99f,
            "ZOH holds $D418 volume-DAC value until next timestamp");

    SidBusSink sink;
    sink.writeSidRegisterPhi2(10u, 0x18u, 0x07u, false);
    sink.writeSidRegisterPhi2(11u, 0x18u, 0x07u, false);
    require(sink.writeCount() == 2u, "PHI2 SID sink does not changed-filter repeated $D418 samples");
    require(sink.write(0).phi2 == 10u && sink.write(1).phi2 == 11u, "PHI2 sink preserves cycles for repeated $D418");

    C64Phi2Machine m;
    SidBusSink machineSink;
    m.powerOn();
    m.attachSidSink(&machineSink);
    m.port().write(0x0000u, 0x2Fu);
    m.port().write(0x0001u, 0x37u); // IO visible
    Phi2BusPhase phase{};
    m.memory().cpuWrite(1234u, 0xD418u, 0x08u, phase, false);
    m.memory().cpuWrite(1235u, 0xD418u, 0x08u, phase, false);
    require(machineSink.writeCount() == 2u, "Memory matrix routes repeated physical $D418 writes");
    require(machineSink.write(0).phi2 == 1234u && machineSink.write(1).phi2 == 1235u,
            "Memory matrix forwards absolute PHI2 timestamps for $D418");

    std::cout << "c64_d418_digi_capture_v706_tests PASS\n";
    return 0;
}
