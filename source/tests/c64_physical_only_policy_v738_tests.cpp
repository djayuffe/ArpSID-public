// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    using namespace ArpSID::C64;

    C64Runtime rt;
    rt.reset(true);
#if ARPSID_C64_PHYSICAL_ONLY
    require(kC64PhysicalOnlyBuild, "physical-only macro maps to true constexpr");
#else
    require(!kC64PhysicalOnlyBuild, "default build is not physical-only");
#endif
    require(!rt.legacyMos6510RuntimePathsReachable(), "legacy Mos6510 runtime paths are retired in every build");
    require((rt.physicalExactnessBlockerMask() & static_cast<uint32_t>(C64PhysicalExactnessBlocker::LegacyMos6510PathPresent)) == 0u,
            "retired legacy path is not exposed as present");

    const std::string cmake = readFile("CMakeLists.txt");
    const std::string runtime = readFile("include/arpsid/core/c64_psid_runtime.h");
    const std::string version = readFile("VERSION.txt");

    require(cmake.find("option(ARPSID_C64_PHYSICAL_ONLY") != std::string::npos,
            "CMake exposes ARPSID_C64_PHYSICAL_ONLY option");
    require(cmake.find("option(ARPSID_C64_PHYSICAL_ONLY \"Keep retired legacy Mos6510 compatibility playback paths unreachable\" ON)") != std::string::npos,
            "production default keeps retired compatibility paths unreachable");
    require(cmake.find("add_compile_definitions(ARPSID_C64_PHYSICAL_ONLY=") != std::string::npos,
            "CMake forwards ARPSID_C64_PHYSICAL_ONLY into compile definitions");
    require(runtime.find("kC64PhysicalOnlyBuild") != std::string::npos,
            "runtime has compile-time physical-only policy constexpr");
    require(runtime.find("c64LegacyMos6510RuntimePathsReachable") != std::string::npos,
            "runtime has named legacy reachability policy helper");
    require(runtime.find("ok = runRsidInitViaPhi2_(phi2Budget);") != std::string::npos,
            "all PSID/RSID init dispatches through PHI2 first");
    require(runtime.find("runPsidVbiPlayViaPhi2_") != std::string::npos,
            "VBI PSID playback has a PHI2-only production path");
    require(runtime.find("++legacyRuntimePlaybackCount_") == std::string::npos,
            "runtime has no legacy Mos6510 playback counter increments");
    require(runtime.find("platform_.runRealtimeSidCoreCycles(cycles, maxInstructions, sid)") == std::string::npos,
            "runtime has no legacy platform machine-cycle fallback");
    require(runtime.find("platform_.runPsidCiaPlaybackServiceTicks(maxTicks)") == std::string::npos &&
            runtime.find("platform_.runPsidCiaPlaybackIrqTicks(maxTicks)") == std::string::npos,
            "runtime has no legacy platform PSID-CIA service fallback");
    require(runtime.find("if (c64LegacyMos6510RuntimePathsReachable())\n            b = b | C64PhysicalExactnessBlocker::LegacyMos6510PathPresent;") != std::string::npos,
            "LegacyMos6510PathPresent blocker tracks runtime reachability policy");
    require(version.find("0.0.690-pass") != std::string::npos,
            "VERSION.txt carries current 0.0.690 pass identity");

    std::cout << "C64PhysicalOnlyPolicyV738Tests PASS\n";
    return 0;
}
