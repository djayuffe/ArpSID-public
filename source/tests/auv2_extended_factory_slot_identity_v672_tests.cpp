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
    const std::string a = readFile(std::string(ARPSID_SOURCE_DIR) + "/source/au2/ArpSIDAUv2Component.mm");
    require(a.find("canonicalFactorySlotFromNormalizedBankSlot(bankNorm)") != std::string::npos,
            "AUv2 state-root bank-slot extraction uses canonical 180-slot decoder");
    require(a.find("canonicalFactorySlotForRoot(bankSlot)") != std::string::npos,
            "AUv2 state-root bank-slot extraction preserves canonical drum slots");
    require(a.find("std::lround(std::clamp(bankNorm, 0.0f, 1.0f) * 127.0f)") == std::string::npos,
            "AUv2 no longer decodes factory bank slot with *127");
    require(a.find("canonicalNormalizedFactoryProgramValue") != std::string::npos,
            "AUv2 factory preset metadata uses factory program helper");
    std::cout << "Auv2ExtendedFactorySlotIdentityV672Tests PASS\n";
    return 0;
}
