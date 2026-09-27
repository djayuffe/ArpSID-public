// Copyright (C) 2024-2026 Ulf Bertilsson
#include <fstream>
#include <iostream>
#include <string>

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    std::ifstream d("include/arpsid/core/sid_host_cycle_dispatcher.h");
    if (!d) d.open("../include/arpsid/core/sid_host_cycle_dispatcher.h");
    std::ifstream v("source/vst3/arpsid_vst3_processor.cpp");
    if (!v) v.open("../source/vst3/arpsid_vst3_processor.cpp");
    if (!d || !v) {
        std::cerr << "cannot open source files\n";
        return 1;
    }
    const std::string disp((std::istreambuf_iterator<char>(d)), {});
    const std::string vst((std::istreambuf_iterator<char>(v)), {});
    auto require = [&](bool ok, const char* msg) {
        if (!ok) {
            std::cerr << "FAIL: " << msg << "\n";
            std::exit(1);
        }
    };

    require(contains(disp, "const bool usePhysicalCycleClockLaw = fractionalCapable || hasResolvedCycleTiming;"),
            "fractional-capable backends always use the physical cycle-clock law");
    require(contains(disp, "if (usePhysicalCycleClockLaw)"),
            "dispatcher no longer switches render law solely on event metadata");
    require(!contains(disp, "if (hasResolvedCycleTiming) {"),
            "old metadata-selected cycle render branch is gone");
    // VST3 reads ProcessContext once into the kernel's TransportState; the
    // kernel is the only tempo/position authority.
    require(contains(vst, "readTransport_(data.processContext, sampleRate_, frames, transport);"),
            "VST path ingests ProcessContext exactly once into the canonical transport");
    require(!contains(vst, "setHostTempo") && !contains(vst, "setHostProjectTimePPQ"),
            "VST path never sets host tempo/PPQ through a second authority");
    return 0;
}
