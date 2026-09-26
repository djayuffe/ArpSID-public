// Copyright (C) 2024-2026 Ulf Bertilsson
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string readText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}
}

int main() {
    const std::filesystem::path testFile = std::filesystem::path(__FILE__);
    const std::filesystem::path root = testFile.parent_path().parent_path().parent_path();
    const auto kernel = readText(root / "source/au3/ArpSIDDSPKernel.hpp");
    const auto phase2 = readText(root / "source/arpsid_processor_phase2.h");
    const auto platform = readText(root / "include/arpsid/core/c64_platform.h");
    const auto bus = readText(root / "include/arpsid/core/c64_bus.h");
    const auto bridge = readText(root / "include/arpsid/core/c64_sid_projection_bridge.h");
    int failures = 0;
    auto require = [&](bool ok, const char* msg) {
        if (!ok) {
            std::cerr << "C64ProjectionMirrorAuthorityV876SourceTests FAIL: " << msg << "\n";
            ++failures;
        }
    };

    require(!kernel.empty(), "failed to read ArpSIDDSPKernel.hpp");
    require(!phase2.empty(), "failed to read arpsid_processor_phase2.h");
    require(!platform.empty(), "failed to read c64_platform.h");
    require(!bus.empty(), "failed to read c64_bus.h");
    require(!bridge.empty(), "failed to read c64_sid_projection_bridge.h");

    require(!contains(kernel, "projectSidRegisterImageThroughC64Bus(\n        c64Platform_"),
            "normal AU3 render path must not reconcile final SID image through C64 bus");
    require(!contains(kernel, "projectSidRegisterImageThroughC64Bus(c64Platform_"),
            "normal AU3 render path must not call final-image reconciliation directly");
    require(contains(kernel, "void cancelC64TelemetryDemandBlock_() noexcept"),
            "early-return observer cancellation helper missing");
    require(contains(kernel, "cancelC64TelemetryDemandBlock_();\n            return;"),
            "SIDPLAY/invalid-output early returns must close observer scope before returning");
    require(contains(kernel, "c64ProjectionMirrorQueuedWritesThisBlock_ > 0u"),
            "first-note block must consume mirror when observer queued writes even if previous audio was idle");
    require(contains(platform, "void clearScheduledProjectionWrites() noexcept"),
            "C64Platform needs projection queue clear for discarded cosmetic mirror blocks");
    require(contains(bus, "bool projectionMirror = false"),
            "projection mirror writes must be tagged so cleanup is selective");
    require(contains(bus, "clearPendingProjectionMirrorEvents"),
            "projection cleanup must not clear ordinary scheduled CPU events");
    require(contains(platform, "scheduleProjectionMirrorCpuWrite"),
            "observer path needs a projection-tagged scheduler");
    require(contains(bridge, "scheduleProjectionMirrorCpuWrite(address, value, phi2Offset)"),
            "host-timed observer writes must stay queued, including sample-zero writes");
    require(contains(phase2, "void runtimeMirrorAppliedProjectionWrite(uint8_t, uint8_t, uint32_t, uint16_t) noexcept {}"),
            "Phase2 target must satisfy canonical applied-write observer contract");

    if (failures != 0) return 1;
    std::cout << "C64ProjectionMirrorAuthorityV876SourceTests PASS\n";
    return 0;
}
