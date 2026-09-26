// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string k = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    require(k.find("arpsid/engines/drum_stem_mixer.h") != std::string::npos,
            "DSP kernel includes explicit drum stem mixer contract");
    require(k.find("resolveDrumStemMixPolicyForFlavor_") != std::string::npos,
            "DSP kernel has an explicit flavor-to-stem-policy resolver");
    require(k.find("DrumStemMixPolicy::ReplaceWithSid808") != std::string::npos,
            "SID808 flavor maps to replacing SID808 policy");
    require(k.find("DrumStemMixPolicy::AdditiveDrumMachine") != std::string::npos,
            "non-SID808 drum flavor maps to additive DrumMachine policy");
    std::cout << "DspKernelStemPolicyContractV652Tests PASS\n";
    return 0;
}
