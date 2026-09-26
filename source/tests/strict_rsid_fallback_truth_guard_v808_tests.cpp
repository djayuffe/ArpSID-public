// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    const std::string runtime = readText("include/arpsid/core/c64_psid_runtime.h");
    const std::size_t fn = runtime.find("C64RunResult runRsidMachineCycles");
    require(fn != std::string::npos, "runRsidMachineCycles not found");
    const std::size_t phi2 = runtime.find("return runRsidPhi2MachineCycles(cycles, maxInstructions, sid);", fn);
    const std::size_t refusal = runtime.find("++strictRsidNotPhi2Count_", fn);
    require(phi2 != std::string::npos, "RSID machine runner must prefer PHI2 execution");
    require(refusal != std::string::npos, "non-PHI2 RSID refusal branch must be present");
    require(phi2 < refusal, "RSID runner tries PHI2 before refusing");
    require(runtime.find("++legacyRuntimePlaybackCount_", fn) == std::string::npos,
            "legacy Mos6510 playback branch must be removed from RSID runner");
    require(runtime.find("platform_.runRealtimeSidCoreCycles", fn) == std::string::npos,
            "RSID runner must not call legacy platform execution");
    require(runtime.find("return empty", refusal) != std::string::npos,
            "non-PHI2 refusal returns an empty run result");

    std::cout << "StrictRsidFallbackTruthGuardV808Tests PASS\n";
    return 0;
}
