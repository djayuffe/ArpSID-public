// Copyright (C) 2024-2026 Ulf Bertilsson
// fix-order #18: AUv2 ramp expansion generates multiple timed parameter
// anchors. A full kernel param-intent queue means individual ramp anchors lose
// sample-accurate timing even though dirty-flush fallback can preserve the last
// value. The AUv2 wrapper must surface generated/drop counts explicitly.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) {
        std::cerr << "cannot open " << rel << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string au2 = readFile("source/au2/ArpSIDAUv2Component.mm");
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");

    require(kernel.find("bool enqueueParameterIntent(") != std::string::npos,
            "kernel enqueueParameterIntent returns queue success/failure");
    require(kernel.find("return queued;") != std::string::npos,
            "kernel returns timed queue result after telemetry update");
    require(kernel.find("paramIntentDirtyFlushFallbackTelemetry_.fetch_add") != std::string::npos,
            "kernel still records dirty-flush fallback on queue failure");

    require(au2.find("auv2RampAnchorScheduledCount") != std::string::npos,
            "AUv2 instance owns ramp-anchor scheduled telemetry");
    require(au2.find("auv2RampAnchorDroppedCount") != std::string::npos,
            "AUv2 instance owns ramp-anchor dropped telemetry");
    require(au2.find("host ramp anchor-drop") != std::string::npos,
            "AUv2 source documents anchor-drop versus generic ingress drop");

    const auto rampCase = au2.find("case kParameterEvent_Ramped:");
    require(rampCase != std::string::npos, "AUv2 ramp scheduling case found");
    const auto rampEnd = au2.find("// do not pin AU PresentPreset from scheduled host parameter ramps", rampCase);
    require(rampEnd != std::string::npos, "AUv2 ramp scheduling case end found");
    const std::string ramp = au2.substr(rampCase, rampEnd - rampCase);

    require(ramp.find("auv2RampAnchorScheduledCount.fetch_add") != std::string::npos,
            "each generated ramp anchor increments scheduled telemetry");
    require(ramp.find("const bool queued = kernel->enqueueParameterIntent") != std::string::npos,
            "AUv2 ramp anchor observes enqueue result");
    require(ramp.find("if (!queued)") != std::string::npos,
            "AUv2 ramp anchor has explicit queue-drop branch");
    require(ramp.find("auv2RampAnchorDroppedCount.fetch_add") != std::string::npos,
            "AUv2 ramp anchor increments dropped telemetry when timed enqueue fails");
    require(ramp.find("return kAudioUnitErr_CannotDoInCurrentContext") == std::string::npos,
            "AUv2 ramp anchor drops are telemetry-only; Logic must not see an AU failure");

    const auto immediateCase = au2.find("case kParameterEvent_Immediate:");
    require(immediateCase != std::string::npos, "AUv2 immediate scheduling case found");
    const std::string immediate = au2.substr(immediateCase, rampCase - immediateCase);
    require(immediate.find("auv2RampAnchorDroppedCount") == std::string::npos,
            "immediate parameter writes are not misreported as ramp anchor drops");
    require(immediate.find("return kAudioUnitErr_CannotDoInCurrentContext") == std::string::npos,
            "AUv2 immediate queue pressure is host-stable and must not trip Logic recovery");

    std::cout << "auv2_ramp_anchor_drop_telemetry_v765_tests: PASS\n";
    return 0;
}
