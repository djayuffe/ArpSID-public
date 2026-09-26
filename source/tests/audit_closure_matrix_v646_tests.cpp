// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const std::string& msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    require(static_cast<bool>(f), "cannot open " + p);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string digi = readFile(root + "/include/arpsid/engines/digi_sampler_engine.h");

    require(kernel.find("target == ArpSID::GUI::KitEngineTarget::DrSID ||\n                       componentFlavor_ == ArpSID::ComponentFlavor::DrumMachine") == std::string::npos,
            "DrumMachine no longer forces DrSID");
    require(kernel.find("constexpr std::uint8_t slotIndex = 0u;") != std::string::npos &&
            kernel.find("oneShot.activeSlotCount = 1u;") != std::string::npos,
            "kernel KIT Digi one-shot uses dense slot0 projection");
    require(digi.find("singleSlotProj.activeSlotCount = static_cast<std::uint8_t>(std::min<int>(") != std::string::npos,
            "sampler direct nonzero Digi trigger keeps active range consistent");

    std::cout << "AuditClosureMatrixV646Tests PASS\n";
    return 0;
}
