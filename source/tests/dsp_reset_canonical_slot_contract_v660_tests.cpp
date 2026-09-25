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
    const std::string k = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    require(k.find("canonicalFactorySlotForRoot(factorySlot)") != std::string::npos,
            "DSP reset preserves canonical drum slots before state-root restore");
    require(k.find("const int slot = ArpSID::normalizeFactoryPatchSlot(factorySlot);") == std::string::npos,
            "DSP reset no longer collapses canonical drum slots through legacy normalize");
    std::cout << "DspResetCanonicalSlotContractV660Tests PASS\n";
    return 0;
}
