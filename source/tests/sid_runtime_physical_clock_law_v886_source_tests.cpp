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
    std::ifstream v("source/arpsid_processor_phase2.cpp");
    if (!v) v.open("../source/arpsid_processor_phase2.cpp");
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
    require(contains(vst, "Host context is already ingested exactly once through the canonical"),
            "VST transport double-ingest block replaced by canonical-only path");
    require(!contains(vst, "const ProcessContext& ctx = *data.processContext;"),
            "VST path no longer re-reads ProcessContext after canonical ingestion");
    require(!contains(vst, "runtimeModel_.setHostTempoBpm(static_cast<float>(hostTempo));"),
            "VST path no longer sets host tempo from a second authority");
    require(!contains(vst, "runtimeModel_.setHostProjectTimePPQ(hostProjectTimePPQ);"),
            "VST path no longer sets host PPQ from a second authority");
    return 0;
}
