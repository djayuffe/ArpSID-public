// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string digiKits = readFile(root + "/include/arpsid/patchbank/factory_digi_kits.h");
    const std::string digiBridge = readFile(root + "/include/arpsid/patchbank/factory_digi_param_bridge.h");
    const std::string drsidKits = readFile(root + "/include/arpsid/patchbank/factory_drsid_kits.h");
    const std::string drumBridge = readFile(root + "/include/arpsid/engines/drum_engine_host_bridge.h");

    require(digiKits.find("makeFactoryDigiPanelModelForSlot") != std::string::npos,
            "Digi factory kit payload source exists");
    require(digiKits.find("digiStepSetActive") != std::string::npos,
            "Digi factory payload contains active steps");
    require(digiBridge.find("makeFactoryDigiPanelModelFromSignature") != std::string::npos,
            "Digi signature is projected into DigiPanelModel");
    require(digiBridge.find("tuneShiftNorm") != std::string::npos &&
            digiBridge.find("startOffsetNorm") != std::string::npos &&
            digiBridge.find("lengthScaleNorm") != std::string::npos &&
            digiBridge.find("sig.loop") != std::string::npos &&
            digiBridge.find("sig.reverse") != std::string::npos,
            "Digi signature fields are used by payload projection");
    require(drsidKits.find("makeFactoryDrSidProgramForSlot") != std::string::npos,
            "DrSID factory microprogram payload source exists");
    require(drsidKits.find("behaviorHash") != std::string::npos,
            "DrSID factory payload includes behavior hash");
    require(drumBridge.find("case DrumContext::Digi4Bit") != std::string::npos &&
            drumBridge.find("unroutedLoadCount") != std::string::npos,
            "Drum bridge Digi rejection policy remains explicit");

    std::cout << "FactoryPayloadFinalDeepGuardV695Tests PASS\n";
    return 0;
}
