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
    const std::string proc = readFile(root + "/source/arpsid_processor_phase2.cpp");
    const std::string params = readFile(root + "/source/parameter_ids.h");
    const std::string forensic = readFile(root + "/source/forensic_patch_bank.cpp");
    const std::string gui = readFile(root + "/source/au3/ArpSIDViewController.mm");

    require(proc.find("std::lround(v * 127.f)") == std::string::npos, "dead kParamBankSlot *127 decode removed");
    require(proc.find("canonicalFactorySlotFromNormalizedBankSlot") != std::string::npos,
            "processor uses canonical bank-slot decoder if event path is revived");
    require(params.find("controller-managed patch bank, 128 slots") == std::string::npos,
            "parameter comment no longer says factory bank is 128 slots");
    require(forensic.find("shipping 128-slot factory bank") == std::string::npos,
            "forensic comment no longer says shipping factory bank is 128 slots");
    require(gui.find("below = 128-slot grid") == std::string::npos,
            "bank panel comment no longer says 128-slot grid");
    std::cout << "NoDead127BankSlotDecodeV694Tests PASS\n";
    return 0;
}
