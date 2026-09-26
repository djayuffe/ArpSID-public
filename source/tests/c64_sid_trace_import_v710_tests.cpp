// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_sid_trace_import.h"
#include <cstdio>
#include <cstdlib>

using namespace ArpSID::C64;

static void require(bool v, const char* msg) {
    if (!v) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}

int main() {
    C64SidBridgeState bridge{};
    C64SidTraceEvent ev[] = {
        {100, 0, 0x18, 0x0A},
        {110, 0, 0x18, 0x0A},
        {120, 0, 0x18, 0x0B},
        {130, 1, 0x00, 0x34},
        {140, 1, 0x01, 0x12},
    };
    auto bs = importC64SidTraceToBridge(bridge, ev, sizeof(ev)/sizeof(ev[0]));
    require(bs.complete, "bridge import must complete for monotonic valid events");
    require(bs.eventsImported == 5u, "all trace events imported");
    require(bs.d418EventsImported == 3u, "all D418 events imported");
    require(bs.repeatedD418EventsImported == 1u, "repeated identical D418 event counted");
    require(bridge.timedWriteCount == 5u, "bridge preserves every trace write");
    require(bridge.d418WriteCount == 3u, "bridge D418 telemetry counts every D418 write");
    require(bridge.d418RepeatedValueWriteCount == 1u, "bridge D418 repeated telemetry preserved");
    require(bridge.timedWrites[0].phi2Cycle == 100u && bridge.timedWrites[1].phi2Cycle == 110u, "absolute PHI2 cycles retained");
    require(bridge.timedWrites[3].chip == 1u && bridge.timedWrites[3].reg == 0x00u, "secondary SID chip retained");
    bridge.resetTimedWrites();
    require(bridge.timedWriteCount == 0u && bridge.timedWriteOverflow == 0u, "resetTimedWrites clears counters");
    require(bridge.timedWrites[0].phi2Cycle == 0u && bridge.timedWrites[1].value == 0u, "resetTimedWrites clears stale front entries");

    ArpSID::SidWriteQueue q{};
    C64SidTraceEvent qev[] = {
        {0, 0, 0x00, 0x11},
        {100, 0, 0x01, 0x22},
        {200, 0, 0x18, 0x03},
    };
    auto qs = importC64SidTraceToQueue(q, qev, 3, 0, 1000.0, 1000.0, 512);
    require(qs.complete, "queue import completes for in-block trace");
    require(q.size() == 3u, "queue receives three trace writes");
    auto view = q.data();
    require(view[0].sampleOffset == 0u && view[1].sampleOffset == 100u && view[2].sampleOffset == 200u, "cycle-to-sample mapping is deterministic");
    require(c64SidFrequencyRegisterFromHz(440.0, 985248.0) > 0u, "MIDI/helper frequency conversion returns SID register");
    require(c64SidTraceDefaultWaveForMidiChannel(0) == 0x40u, "MIDI ch0 maps pulse");
    require(c64SidTraceDefaultWaveForMidiChannel(1) == 0x20u, "MIDI ch1 maps saw");
    require(c64SidTraceDefaultWaveForMidiChannel(9) == 0x80u, "MIDI ch9 maps noise drums");
    require(c64SidTraceDefaultWaveForMidiChannel(2) == 0x10u, "other MIDI channels map triangle");

    C64SidTraceEvent bad[] = {{5, 7, 0x18, 0}, {4, 0, 0x18, 0}};
    auto badStats = importC64SidTraceToBridge(bridge, bad, 2);
    require(!badStats.complete, "invalid/non-monotonic trace cannot be complete");
    require(badStats.eventsRejected == 1u, "invalid chip rejected");
    require(!badStats.monotonic, "non-monotonic trace detected");

    std::puts("c64_sid_trace_import_v710_tests: PASS");
    return 0;
}
