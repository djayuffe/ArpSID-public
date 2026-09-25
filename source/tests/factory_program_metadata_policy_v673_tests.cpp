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
    const std::string dsp = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string au3 = readFile(root + "/source/au3/ArpSIDAudioUnit.mm");
    const std::string au2 = readFile(root + "/source/au2/ArpSIDAUv2Component.mm");
    require(dsp.find("kParamProgram) return ArpSID::canonicalNormalizedFactoryProgramValue") != std::string::npos,
            "DSP sticky kParamProgram uses factory helper");
    require(au3.find("const float progNorm = ArpSID::canonicalNormalizedFactoryProgramValue((int)normalizedSlot);") != std::string::npos,
            "AUv3 preset mirror uses factory helper");
    require(au2.find("canonicalNormalizedFactoryProgramValue") != std::string::npos,
            "AUv2 preset mirror uses factory helper");
    require(dsp.find("canonicalNormalizedProgramValue(static_cast<uint8_t>(ArpSID::normalizeFactoryPatchSlot(stickyPresetDisplaySlot())))") == std::string::npos,
            "DSP no longer wraps factory identity through 7-bit program helper");
    require(au3.find("canonicalNormalizedProgramValue((uint8_t)normalizedSlot)") == std::string::npos,
            "AUv3 no longer wraps factory identity through 7-bit program helper");
    std::cout << "FactoryProgramMetadataPolicyV673Tests PASS\n";
    return 0;
}
