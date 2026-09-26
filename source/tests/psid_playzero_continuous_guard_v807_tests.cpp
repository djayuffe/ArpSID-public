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
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");

    require(runtime.find("runContinuousMachineCycles") != std::string::npos,
            "C64Runtime must expose explicit continuous-machine runner");
    require(runtime.find("if (loaded_.header.rsid) return runRsidMachineCycles") != std::string::npos,
            "continuous runner must preserve RSID strict/compatible policy by delegating RSID");
    require(runtime.find("if (loaded_.header.playAddress != 0u) return empty") != std::string::npos,
            "continuous runner must be limited to PSID playAddress==0 for non-RSID images");
    require(runtime.find("runPsidPhi2MachineCycles(cycles, maxInstructions, sid)") != std::string::npos,
            "PSID playAddress==0 continuous runner must clock the PHI2 machine");
    require(runtime.find("platform_.runRealtimeSidCoreCycles(cycles, maxInstructions, sid)") == std::string::npos,
            "PSID playAddress==0 continuous runner must not fall back to the legacy machine");
    require(kernel.find("runContinuousMachineCycles(catchup, kC64RsidMaxInstructionsPerAudioBlock, &c64SidBridge_)") != std::string::npos,
            "kernel continuous path must call runContinuousMachineCycles, not RSID-only runner");

    std::cout << "PsidPlayZeroContinuousGuardV807Tests PASS\n";
    return 0;
}
