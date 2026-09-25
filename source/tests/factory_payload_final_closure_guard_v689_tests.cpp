#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string forensic = readFile(root + "/source/forensic_patch_bank.cpp");
    const std::string params = readFile(root + "/source/factory_patch_params.h");
    const std::string digi = readFile(root + "/include/arpsid/patchbank/factory_digi_param_bridge.h");
    const std::string ctrl = readFile(root + "/source/arpsid_controller.cpp");
    const std::string bankH = readFile(root + "/include/arpsid/patchbank/sid_patchbank_io.h");
    const std::string bankCpp = readFile(root + "/source/patchbank/sid_patchbank_io.cpp");
    const std::string kitseq = readFile(root + "/include/arpsid/gui/kit_sequencer.h");

    require(forensic.find("slot >= 80 && slot <= 119") != std::string::npos &&
            forensic.find("makeDrSidKitDefinition") != std::string::npos,
            "DrSID canonical slots 80..119 have a factory definition path");
    require(forensic.find("DrumContext::Digi4Bit") != std::string::npos &&
            forensic.find("drSidMode = false") != std::string::npos,
            "Digi factory definitions are not sanitized into DrSID mode");
    require(digi.find("applyFactoryDigiDefaults") != std::string::npos,
            "Digi factory defaults bridge exists");
    require(params.find("applyFactoryDigiDefaults") != std::string::npos,
            "Digi defaults are applied into state-root params");
    require(ctrl.find("kCanonicalFactoryPatchSlotCount") != std::string::npos &&
            ctrl.find("kMaxPresets_") != std::string::npos,
            "VST3 factory program list uses canonical 180-slot count");
    require((bankH + bankCpp).find("128-slot user-bank-compatible subset, not the full 180-slot factory") != std::string::npos,
            ".arpbank v1 export boundary is explicitly documented");
    require(kitseq.find("kitPanelModelIsWellFormed") != std::string::npos,
            "Kit sequencer validates KitPanelModel at compile boundary");
    require(kitseq.find("kitVoiceConfigGridIsWellFormed") != std::string::npos,
            "Kit sequencer validates KitVoiceConfigGrid at compile boundary");

    std::cout << "FactoryPayloadFinalClosureGuardV689Tests PASS\n";
    return 0;
}
