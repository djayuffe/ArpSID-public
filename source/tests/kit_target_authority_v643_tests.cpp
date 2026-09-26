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

    const std::string sid808 = "if (target == ArpSID::GUI::KitEngineTarget::SID808";
    const std::string drsid = "} else if (target == ArpSID::GUI::KitEngineTarget::DrSID) {";
    const std::string digi  = "} else if (target == ArpSID::GUI::KitEngineTarget::Digi) {";

    require(k.find(sid808) != std::string::npos, "SID808 target branch exists");
    require(k.find(drsid) != std::string::npos, "DrSID target branch is explicit");
    require(k.find(digi) != std::string::npos, "Digi target branch is explicit");

    require(k.find("target == ArpSID::GUI::KitEngineTarget::DrSID ||\n                       componentFlavor_ == ArpSID::ComponentFlavor::DrumMachine") == std::string::npos,
            "DrumMachine flavor no longer forces non-SID808 KIT events into DrSID branch");

    const auto dpos = k.find(drsid);
    const auto digipos = k.find(digi);
    require(dpos != std::string::npos && digipos != std::string::npos && dpos < digipos,
            "routing order is SID808, DrSID, Digi");

    std::cout << "KitTargetAuthorityV643Tests PASS\n";
    return 0;
}
